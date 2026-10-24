cmake_minimum_required(VERSION 3.21)
project(osdos VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Strip qDebug(...) calls in Release builds to reduce CPU/disk noise on Pi
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -DQT_NO_DEBUG_OUTPUT")

# Release version string. CI passes the git tag (-DAPP_VERSION=vX.Y.Z); local
# builds report "dev", which disables the in-app self-update download path.
set(APP_VERSION "dev" CACHE STRING "Release version (CI passes the git tag)")

# The build, for Settings → About: the commit the tree was configured from and
# the day. CI configures each build afresh; a local build keeps its configure's
# commit until the next one. A tree without git (a source archive) has the
# day alone.
execute_process(COMMAND git -C "${CMAKE_SOURCE_DIR}" rev-parse --short=7 HEAD
    OUTPUT_VARIABLE APP_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE APP_COMMIT_RESULT ERROR_QUIET)
string(TIMESTAMP APP_BUILD_DATE "%Y-%m-%d" UTC)
if(APP_COMMIT_RESULT EQUAL 0 AND NOT APP_COMMIT STREQUAL "")
    set(APP_BUILD "${APP_COMMIT} ${APP_BUILD_DATE}")
else()
    set(APP_BUILD "${APP_BUILD_DATE}")
endif()

include(GNUInstallDirs)

# The systems the app builds on: Linux (the Pi's, and any desktop), macOS,
# Windows (x64, MSVC), and the BSDs (FreeBSD, OpenBSD), which take a
# desktop's paths. What only one of them has is chosen by these, not by "not
# the other".
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(OSDOS_LINUX ON)
else()
    set(OSDOS_LINUX OFF)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Concurrent Gui Quick Qml Network Svg)
find_package(OpenSSL REQUIRED)
find_package(SDL2 CONFIG REQUIRED) # gamepad input (InputManager)

# libdrm is used on Linux/RPi to save and restore the KMS CRTC state
# around mpv playback so Qt EGLFS can resume cleanly after mpv exits.
if(OSDOS_LINUX)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(Libdrm REQUIRED libdrm)
endif()

# PC/SC for the NFC reader module. Optional: the module's other driver talks to
# a PN532 over USB serial through plain termios, so readers still work without
# it — which is what makes the NFC module usable on immutable distros where
# pcscd cannot be installed. macOS ships PCSC.framework with the OS; Linux needs
# libpcsclite-dev, the BSDs the pcsc-lite package. Not on Windows yet (its
# winscard differs in its types), where the module says it is not there.
if(APPLE)
    find_library(PCSC_LIBRARIES PCSC)
    if(PCSC_LIBRARIES)
        set(PCSC_FOUND TRUE)
    endif()
elseif(UNIX)
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(PCSC IMPORTED_TARGET libpcsclite)
    endif()
endif()
if(NOT PCSC_FOUND)
    message(STATUS "PC/SC not found — NFC reader module will build with PN532 USB serial support only")
endif()

if(APPLE)
    enable_language(OBJCXX)
endif()

add_executable(osdos
    src/main.cpp
    src/util/LegacyNames.cpp
    src/util/Board.cpp
    src/AppCore.cpp
    src/modules/local_files/LocalFilesBackend.cpp
    src/modules/local_files/RemovableDrives.cpp
    src/modules/plex/PlexBackend.cpp
    src/modules/jellyfin/JellyfinBackend.cpp
    src/modules/emby/EmbyBackend.cpp
    src/modules/ambient_mode/AmbientModeBackend.cpp
    src/modules/youtube/YouTubeBackend.cpp
    src/modules/playlists/PlaylistsBackend.cpp
    src/modules/playlists/ServerDownload.cpp
    src/modules/nfc_reader/NfcReaderBackend.cpp
    src/modules/nfc_reader/PcscDriver.cpp
    src/modules/nfc_reader/Pn532SerialDriver.cpp
    src/modules/nfc_reader/SerialPort.cpp
    src/modules/weather/WeatherBackend.cpp
    src/modules/scripts/ScriptsBackend.cpp
    src/modules/scripts/ScriptLauncher.cpp
    src/modules/arcade/ArcadeBackend.cpp
    src/modules/arcade/Mame.cpp
    src/modules/arcade/Libretro.cpp
    src/modules/arcade/FbneoInstaller.cpp
    src/modules/arcade/RetroAchievements.cpp
    src/modules/arcade/ArcadeDb.cpp
    src/modules/highscores/HighScoresBackend.cpp
    src/modules/highscores/ScoreTable.cpp
    src/modules/web_player/WebPlayerBackend.cpp
    src/modules/web_player/TmdbCatalog.cpp
    src/player/MpvController.cpp
    src/player/EmbeddedMpv.cpp
    src/player/VideoSurface.cpp
    src/player/VhsNoise.cpp
    src/fx/SelectorFx.cpp
    src/util/YtDlpLocator.cpp
    src/util/MpvLocator.cpp
    src/util/ExecPath.cpp
    src/util/DisplayHandoff.cpp
    src/util/OsdIconProvider.cpp
    src/util/OsdSkinProvider.cpp
    src/util/FontconfigOverride.cpp
    src/util/Diagnostics.cpp
    src/input/InputManager.cpp
    src/input/ControllerProfile.cpp
    src/input/IdleTracker.cpp
    src/input/JammaPins.cpp
    src/update/UpdateManager.cpp
    src/boot/BootProgress.cpp
    src/display/DisplayOutput.cpp
    src/audio/AudioOutput.cpp
    src/audio/MenuMusic.cpp
    src/bluetooth/BluetoothManager.cpp
    src/wifi/WifiManager.cpp
    src/system/SystemInfo.cpp
    src/util/RootRequest.cpp
    $<$<BOOL:${APPLE}>:src/util/MacosUtils.mm>
)

target_compile_definitions(osdos PRIVATE APP_VERSION="${APP_VERSION}" APP_BUILD="${APP_BUILD}")

# MSVC: the standard's C++ (Qt 6 asks for both flags), sources in UTF-8, and
# windows.h without its min/max macros and the rarely needed parts. SDL's header
# renames main() to SDL_main on Windows, for its own SDL2main library to call
# from a WinMain of its own; the app keeps its main (SDL_MAIN_HANDLED) and
# tells SDL so before SDL_Init (InputManager).
if(MSVC)
    target_compile_options(osdos PRIVATE /permissive- /Zc:__cplusplus /utf-8)
    target_compile_definitions(osdos PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS SDL_MAIN_HANDLED)
endif()

# The app's own functions in its dynamic symbol table (-rdynamic), so that
# a crash or a stall report's backtrace names them (util/Diagnostics).
if(UNIX AND NOT APPLE)
    set_target_properties(osdos PROPERTIES ENABLE_EXPORTS ON)
endif()

target_link_libraries(osdos PRIVATE
    Qt6::Core
    Qt6::Concurrent
    Qt6::Gui
    Qt6::Quick
    Qt6::Qml
    Qt6::Network
    Qt6::Svg
    OpenSSL::Crypto
    SDL2::SDL2
    $<$<BOOL:${OSDOS_LINUX}>:${Libdrm_LIBRARIES}>
)

if(APPLE)
    target_link_libraries(osdos PRIVATE "-framework AppKit")
endif()

if(PCSC_FOUND)
    target_compile_definitions(osdos PRIVATE OSDOS_NFC_PCSC_AVAILABLE=1)
    if(APPLE)
        target_link_libraries(osdos PRIVATE ${PCSC_LIBRARIES})
    else()
        target_link_libraries(osdos PRIVATE PkgConfig::PCSC)
    endif()
endif()

if(OSDOS_LINUX)
    target_include_directories(osdos PRIVATE ${Libdrm_INCLUDE_DIRS})
endif()

# libmpv's headers, for Transparent Background: video played inside the app's
# own window (src/player/EmbeddedMpv). Optional, and only the headers: libmpv
# itself is opened at run time, so the app still runs where it isn't installed
# (libmpv-dev on Debian / Raspberry Pi OS; Homebrew's mpv on macOS).
if(NOT PKG_CONFIG_FOUND)
    find_package(PkgConfig QUIET)
endif()
if(PKG_CONFIG_FOUND)
    pkg_check_modules(MPV QUIET mpv)
endif()
if(MPV_FOUND)
    target_compile_definitions(osdos PRIVATE OSDOS_EMBEDDED_MPV=1)
    # client.h and render.h need nothing of the dependencies pkg-config lists.
    target_include_directories(osdos PRIVATE ${MPV_INCLUDEDIR})
else()
    message(STATUS "libmpv headers not found — Transparent Background will be unavailable")
endif()

# Qt Shader Tools compiles the menus' effects (shaders/: the screen and text
# effects, the backgrounds, the ripple between windows) into the binary, for
# every graphics API Qt draws with: OpenGL (ES on the Pi), Metal, Direct3D.
# Optional: without it the app builds and runs, those effects left out
# (qt6-shadertools-dev on Debian / Raspberry Pi OS; Qt Shader Tools in Qt's
# installer). The selector's effects and the cube need none of it.
find_package(Qt6 QUIET COMPONENTS ShaderTools)
if(Qt6ShaderTools_FOUND)
    qt_add_shaders(osdos "osdos_shaders"
        PREFIX "/"
        FILES
            shaders/effects.frag
            shaders/bg-matrix.frag
            shaders/bg-fire.frag
            shaders/bg-stars.frag
            shaders/bg-snow.frag
            shaders/bg-flag.frag
            shaders/bg-plasma.frag
            shaders/ripple.frag
            shaders/wave.frag
            shaders/drop.frag)
else()
    message(STATUS "Qt Shader Tools not found — the shader effects will be unavailable")
endif()

# BlueZ and NetworkManager over D-Bus for Settings → Bluetooth and Wi-Fi
# (src/bluetooth, src/wifi). Linux only: a Mac or a PC pairs devices and joins
# networks in its own settings. Without Qt D-Bus the rows are left out.
if(OSDOS_LINUX)
    find_package(Qt6 QUIET COMPONENTS DBus)
endif()
if(Qt6DBus_FOUND)
    target_sources(osdos PRIVATE src/bluetooth/BluetoothAgent.cpp)
    target_compile_definitions(osdos PRIVATE OSDOS_BLUETOOTH=1 OSDOS_WIFI=1)
    target_link_libraries(osdos PRIVATE Qt6::DBus)
else()
    message(STATUS "Qt D-Bus not found — Settings → Bluetooth and Wi-Fi will be unavailable")
endif()

# openhi2txt, for the High Scores module's tables (src/modules/highscores): it
# reads MAME's high score files through hi2txt's game definitions, a zip it
# opens with minizip. Optional: without minizip the module is there but says
# its tables were left out. The Arcade module unpacks the FinalBurn Neo core it
# downloads with minizip too (OSDOS_MINIZIP).
include(cmake/openhi2txt.cmake)
if(OPENHI2TXT_FOUND)
    target_compile_definitions(osdos PRIVATE OSDOS_HIGH_SCORES=1 OSDOS_MINIZIP=1)
    target_include_directories(osdos PRIVATE ${MINIZIP_INCLUDE_DIR})
    target_link_libraries(osdos PRIVATE openhi2txt::openhi2txt ${MINIZIP_LIBRARY})
else()
    message(STATUS "minizip not found — High Scores will show no tables")
endif()

target_include_directories(osdos PRIVATE
    src
)

# osdos-jamma: a Raspberry Pi to JAMMA board's controls (two MCP23017s on I2C)
# as two gamepads, through uinput (src/jamma). Linux only, and no Qt: the OS
# image runs it as a service of its own, from the app's folder.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(osdos-jamma src/jamma/main.cpp src/jamma/JammaPanel.cpp)
    set_target_properties(osdos-jamma PROPERTIES AUTOMOC OFF)

    # osdos_signin_libretro: a libretro core that plays nothing, for RetroArch
    # to sign in to RetroAchievements with as the account is typed in
    # (libretro::SignIn), beside the app, which finds it there. Linux only:
    # elsewhere the sign-in waits for the next game, as before.
    enable_language(C)
    add_library(osdos_signin_libretro MODULE src/modules/arcade/signin/osdos_signin_libretro.c)
    set_target_properties(osdos_signin_libretro PROPERTIES
        PREFIX ""
        C_VISIBILITY_PRESET hidden
        AUTOMOC OFF
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
endif()

# ── macOS bundle metadata ──────────────────────────────────────────────────────
set_target_properties(osdos PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "OSD-OS"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.osdos.app"
    MACOSX_BUNDLE_ICON_FILE "packaging/macos/osd-os"
    INSTALL_RPATH_USE_LINK_PATH TRUE
)

# ── Dev bundle symlink (macOS only, non-CI) ───────────────────────────────────
# Makes double-clicking build/osdos.app work during development by symlinking
# Contents/Resources → repo root so resolveAppRoot() finds the QML files.
# Skipped in CI (ENV{CI} is set by GitHub Actions) because cmake --install
# populates Resources with real files — a symlink in the build tree would be
# packaged into the DMG pointing to a CI path that doesn't exist on end-user Macs.
if(APPLE AND NOT DEFINED ENV{CI})
    add_custom_command(TARGET osdos POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E remove -f
            "$<TARGET_BUNDLE_CONTENT_DIR:osdos>/Resources"
        COMMAND ${CMAKE_COMMAND} -E create_symlink
            "${CMAKE_SOURCE_DIR}"
            "$<TARGET_BUNDLE_CONTENT_DIR:osdos>/Resources"
        COMMENT "Symlinking repo root as bundle Resources (dev build)"
    )
endif()

# ── Install rules ──────────────────────────────────────────────────────────────
if(APPLE)
    install(TARGETS osdos BUNDLE DESTINATION . COMPONENT Runtime)

    # Use a hardcoded relative path — $<TARGET_BUNDLE_CONTENT_DIR> resolves to
    # the build tree, not the install prefix, inside install() rules.
    # LICENSE goes with every copy, as the GPL asks; Settings → About shows it.
    install(FILES "${CMAKE_SOURCE_DIR}/Main.qml" "${CMAKE_SOURCE_DIR}/LICENSE"
        DESTINATION "osdos.app/Contents/Resources"
        COMPONENT Runtime)

    install(DIRECTORY
        "${CMAKE_SOURCE_DIR}/views"
        "${CMAKE_SOURCE_DIR}/modules"
        "${CMAKE_SOURCE_DIR}/assets"
        DESTINATION "osdos.app/Contents/Resources"
        COMPONENT Runtime)
    # Of scripts/, what the app runs: mpv's Lua scripts and the web player's
    # browser launcher; not the build and release tooling beside them.
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/scripts"
        DESTINATION "osdos.app/Contents/Resources"
        COMPONENT Runtime
        FILES_MATCHING PATTERN "*.lua" PATTERN "web-player.sh")

    # Icon lives under packaging/macos (not assets/) so it ships alongside the
    # other macOS packaging inputs; installed at the same relative path so it
    # keeps matching MACOSX_BUNDLE_ICON_FILE above.
    install(DIRECTORY
        "${CMAKE_SOURCE_DIR}/packaging/macos"
        DESTINATION "osdos.app/Contents/Resources/packaging"
        COMPONENT Runtime)

    # macdeployqt is NOT run here — it requires Qt frameworks to be present and
    # is environment-specific. Run it manually in CI after cmake --install.
else()
    install(TARGETS osdos
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
        COMPONENT Runtime)
    if(TARGET osdos-jamma)
        install(TARGETS osdos-jamma
            RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
            COMPONENT Runtime)
    endif()
    if(TARGET osdos_signin_libretro)
        install(TARGETS osdos_signin_libretro
            LIBRARY DESTINATION "${CMAKE_INSTALL_BINDIR}"
            COMPONENT Runtime)
    endif()

    # LICENSE goes with every copy, as the GPL asks; Settings → About shows it.
    install(FILES "${CMAKE_SOURCE_DIR}/Main.qml" "${CMAKE_SOURCE_DIR}/LICENSE"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/osdos"
        COMPONENT Runtime)

    install(DIRECTORY
        "${CMAKE_SOURCE_DIR}/views"
        "${CMAKE_SOURCE_DIR}/modules"
        "${CMAKE_SOURCE_DIR}/assets"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/osdos"
        COMPONENT Runtime)
    # Of scripts/, what the app runs: mpv's Lua scripts and the web player's
    # browser launcher; not the build and release tooling beside them.
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/scripts"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/osdos"
        COMPONENT Runtime
        FILES_MATCHING PATTERN "*.lua" PATTERN "web-player.sh")
endif()
