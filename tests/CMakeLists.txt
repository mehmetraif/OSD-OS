cmake_minimum_required(VERSION 3.21)
project(osdos_regression_tests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Core Concurrent Gui Network Qml Quick QuickTest Test)
enable_testing()

# The app's sources that several tests are built with, each built once as a
# static library the tests link: none of them is built otherwise for one
# test than for another (no definition of a test's own reaches them). The
# app's names from before OSD/OS (LegacyNames); AppCore with the files it
# reads and writes through; and the sound, the menu music and the other
# mpvs that play beside it.
add_library(osdos_test_names STATIC ../src/util/LegacyNames.cpp)
target_include_directories(osdos_test_names PUBLIC ../src)
target_link_libraries(osdos_test_names PUBLIC Qt6::Core)
add_library(osdos_test_core STATIC
    ../src/AppCore.cpp
    ../src/util/LogSaver.cpp
    ../src/util/Looks.cpp
    ../src/util/SetupFile.cpp)
target_link_libraries(osdos_test_core PUBLIC osdos_test_names Qt6::Concurrent Qt6::Network Qt6::Qml)
add_library(osdos_test_audio STATIC
    ../src/audio/AudioOutput.cpp
    ../src/audio/MenuMusic.cpp
    ../src/audio/BackgroundMpv.cpp
    ../src/util/MpvLocator.cpp)
target_link_libraries(osdos_test_audio PUBLIC osdos_test_core Qt6::Network)

# A C++ test: <name>_test, built of <name>_test.cpp and SOURCES with src/ to
# include and DEFINITIONS, linked with LIBRARIES and Qt's Core and Test, and
# run as <name>, with its TIMEOUT and ENVIRONMENT where it has them.
function(osdos_cpp_test name)
    cmake_parse_arguments(arg "" "TIMEOUT" "SOURCES;LIBRARIES;DEFINITIONS;ENVIRONMENT" ${ARGN})
    add_executable(${name}_test ${name}_test.cpp ${arg_SOURCES})
    target_include_directories(${name}_test PRIVATE ../src)
    if(arg_DEFINITIONS)
        target_compile_definitions(${name}_test PRIVATE ${arg_DEFINITIONS})
    endif()
    target_link_libraries(${name}_test PRIVATE ${arg_LIBRARIES} Qt6::Core Qt6::Test)
    add_test(NAME ${name} COMMAND ${name}_test)
    if(arg_ENVIRONMENT)
        set_tests_properties(${name} PROPERTIES ENVIRONMENT "${arg_ENVIRONMENT}")
    endif()
    if(arg_TIMEOUT)
        set_tests_properties(${name} PROPERTIES TIMEOUT ${arg_TIMEOUT})
    endif()
endfunction()

osdos_cpp_test(reliability
    SOURCES ../src/modules/playlists/ServerDownload.cpp ../src/modules/weather/WeatherBackend.cpp
            ../src/update/UpdateManager.cpp ../src/util/LogFile.cpp
    DEFINITIONS APP_VERSION="test" OSDOS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/.."
    LIBRARIES osdos_test_audio Qt6::Concurrent Qt6::Network Qt6::Qml)
# What a view says under "… is not installed", for the system OSD/OS runs
# on (util/InstallHint.h).
osdos_cpp_test(install_hint)
# The small files the app reads whole, and its JSON told apart from a file
# that isn't there and one that is broken (util/FileText.h).
osdos_cpp_test(file_text)
# The signals that end the app (SIGTERM, SIGINT, SIGHUP), handled from the
# event loop with nothing polling for them (util/TermSignals).
if(NOT WIN32)
    osdos_cpp_test(term_signals SOURCES ../src/util/TermSignals.cpp)
endif()

# The 7z extractor (the LZMA SDK's decoder, built in as in the app) on
# archives made with py7zr (data/arcade/*.7z): what is in them, taken out
# whole with its folders, the limits on their size, a damaged one, and an
# entry named to leave the folder, which is skipped.
include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/lzma.cmake)
# The engines' versions (scripts/engines.env), as the app has them.
include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/engines.cmake)
osdos_cpp_test(sevenzip SOURCES ../src/util/SevenZip.cpp
    DEFINITIONS TESTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}" LIBRARIES osdos_lzma)
# Settings → System → Export to OSD/OS Setup: the settings gathered as the
# page's file, cut into the links of QR codes, and drawn as codes.
include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/qrcodegen.cmake)
osdos_cpp_test(setup_export SOURCES ../src/util/SetupExport.cpp
    DEFINITIONS SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/.." LIBRARIES Qt6::Gui osdos_qrcodegen)
find_program(LUA_EXECUTABLE NAMES lua5.4 lua54 lua REQUIRED)   # lua54: the BSDs' packages
add_test(NAME osc_controls COMMAND ${LUA_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/osc_test.lua
                                  ${CMAKE_CURRENT_SOURCE_DIR}/../scripts/mpv-osc.lua)
# The media-key script: the volume bar, and a remote's channel keys through
# a playlist.
add_test(NAME media_keys COMMAND ${LUA_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/media_keys_test.lua
                                ${CMAKE_CURRENT_SOURCE_DIR}/../scripts/mpv-media-keys.lua)
osdos_cpp_test(storage_search
    SOURCES ../src/modules/local_files/LocalFilesBackend.cpp ../src/modules/local_files/RemovableDrives.cpp
    LIBRARIES osdos_test_core Qt6::Concurrent Qt6::Network Qt6::Qml)

# Themes and skins, read from the app's folder and the data folder's
# (util/Looks, through AppCore), and their icons as the icon provider draws
# and keeps them (OsdIconProvider).
osdos_cpp_test(looks SOURCES ../src/util/OsdIconProvider.cpp
    DEFINITIONS OSDOS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/.."
    LIBRARIES osdos_test_core Qt6::Gui Qt6::Network Qt6::Qml Qt6::Quick)

# The items that draw in art pixels (VhsNoise and SelectorFx, on
# ArtPixelItem): what they draw from a given seed, frame by frame, and on the
# screen, scaled up without smoothing; when they run; their properties as
# QML sets them. Its floats as IEEE has them, a*b+c never fused into one step
# where the processor can (arm64), so that the sparks' frames are the same on
# every machine.
osdos_cpp_test(art_pixels
    SOURCES ../src/fx/ArtPixelItem.cpp ../src/player/VhsNoise.cpp ../src/fx/SelectorFx.cpp
    LIBRARIES Qt6::Gui Qt6::Qml Qt6::Quick
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
if(NOT MSVC)
    target_compile_options(art_pixels_test PRIVATE -ffp-contract=off)
endif()

# The Jellyfin, Emby and Plex backends against a stand-in network: a list
# asked for after another drops the first's reply, lists kept until a playback
# ends, a stream given up dropped unreported, an auth check that changes
# nothing quiet, and Plex's next item for a player left dropped.
find_package(OpenSSL REQUIRED)
osdos_cpp_test(media_servers
    SOURCES ../src/util/EmbyApiBackend.cpp ../src/modules/jellyfin/JellyfinBackend.cpp
            ../src/modules/emby/EmbyBackend.cpp ../src/modules/plex/PlexBackend.cpp
    LIBRARIES osdos_test_core Qt6::Concurrent Qt6::Network Qt6::Qml OpenSSL::Crypto)

# An NFC card's file: mapped again, the old one kept till the new one is
# written, and a long title cut to a name every filesystem takes.
osdos_cpp_test(nfc_cards
    SOURCES ../src/modules/nfc_reader/NfcReaderBackend.cpp ../src/modules/nfc_reader/PcscDriver.cpp
            ../src/modules/nfc_reader/Pn532SerialDriver.cpp ../src/modules/nfc_reader/SerialPort.cpp
    LIBRARIES osdos_test_core Qt6::Network Qt6::Qml)

# The High Scores module on MAME's files of three games (data/highscores),
# read by openhi2txt with hi2txt's definitions: the tables as lines, what is
# kept and read again, the games' names, the files that can't be read. Where
# minizip is found, as the module's tables are in the app.
include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/openhi2txt.cmake)
if(OPENHI2TXT_FOUND)
    osdos_cpp_test(highscores
        SOURCES ../src/modules/highscores/HighScoresBackend.cpp ../src/modules/highscores/ScoreTable.cpp
                ../src/modules/arcade/Mame.cpp
        DEFINITIONS OSDOS_HIGH_SCORES=1 APP_DIR="${CMAKE_CURRENT_SOURCE_DIR}/.."
        LIBRARIES Qt6::Concurrent openhi2txt::openhi2txt)
else()
    message(WARNING "minizip not found: the High Scores test is left out")
endif()

# The tests built and run on Linux alone.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(Libdrm REQUIRED libdrm)

    # More of the app's sources that several of them are built with, once:
    # the board and the requests to the system's root helpers; what starts a
    # program with the screen (a script, the web player, a game) and finds
    # mpv's and yt-dlp's programs; and YouTube with the web player it plays
    # through.
    add_library(osdos_test_system STATIC ../src/util/Board.cpp ../src/util/RootRequest.cpp)
    target_link_libraries(osdos_test_system PUBLIC osdos_test_names Qt6::Network)
    add_library(osdos_test_launch STATIC
        ../src/modules/scripts/ScriptLauncher.cpp
        ../src/input/ControllerProfile.cpp
        ../src/util/DisplayHandoff.cpp
        ../src/util/YtDlpLocator.cpp)
    target_include_directories(osdos_test_launch PUBLIC ${Libdrm_INCLUDE_DIRS})
    target_link_libraries(osdos_test_launch PUBLIC osdos_test_audio ${Libdrm_LIBRARIES})
    add_library(osdos_test_youtube STATIC
        ../src/modules/youtube/YouTubeBackend.cpp
        ../src/modules/web_player/WebPlayerBackend.cpp
        ../src/modules/web_player/TmdbCatalog.cpp)
    target_link_libraries(osdos_test_youtube PUBLIC osdos_test_launch Qt6::Network)

    # One video handed over to the next (MpvController), against a stand-in
    # for mpv. Linux only: the stand-in is a shell script that looks in /proc.
    # Its stand-in for mpv runs as a process and the controller's waits for
    # real, test after test (a start watched, a player ended, a session given
    # up on): past the minute CI gives a test without a timeout of its own.
    osdos_cpp_test(playback_retire
        SOURCES ../src/player/MpvController.cpp ../src/player/EmbeddedMpv.cpp ../src/player/VideoSurface.cpp
                ../src/util/Diagnostics.cpp ../src/util/ExecPath.cpp ../src/util/FontconfigOverride.cpp
        LIBRARIES osdos_test_launch osdos_test_system Qt6::Gui Qt6::Quick Qt6::Qml Qt6::Network
        TIMEOUT 180)
    # libmpv's headers where there are any (as the app's own build takes
    # them), so that a video played inside the window (EmbeddedMpv) is tested
    # too; without them, or without libmpv itself at run time, that test is
    # skipped.
    pkg_check_modules(MPV QUIET mpv)
    if(MPV_FOUND)
        target_compile_definitions(playback_retire_test PRIVATE OSDOS_EMBEDDED_MPV=1)
        target_include_directories(playback_retire_test PRIVATE ${MPV_INCLUDEDIR})
    endif()

    # The menu music, against a stand-in for mpv: it plays in the menus, and
    # stops at once when anything else is about to play sound. And the app's
    # other audio mpvs beside it, Weather's music and Ambient Mode's audio.
    osdos_cpp_test(menu_music
        SOURCES ../src/modules/ambient_mode/AmbientModeBackend.cpp ../src/modules/weather/WeatherBackend.cpp
        DEFINITIONS APP_VERSION="test" LIBRARIES osdos_test_audio Qt6::Network Qt6::Qml)

    # The crash and stall reports (util/Diagnostics), from child processes
    # that crash or freeze on purpose. Linux only, as the reports are.
    osdos_cpp_test(diagnostics SOURCES ../src/util/Diagnostics.cpp)

    # Settings → Screen → Display Output against the image's presets in a stand-in boot
    # partition: which are offered, by board and launcher API, and At Next
    # Start through a stand-in for its socket.
    osdos_cpp_test(display_output SOURCES ../src/display/DisplayOutput.cpp
        DEFINITIONS PRESETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../os/stage-osdos/03-boot/files"
                    INSTALL_SH="${CMAKE_CURRENT_SOURCE_DIR}/../scripts/install.sh"
        LIBRARIES osdos_test_system Qt6::Network)
    qt_add_resources(display_output_test "display_presets" PREFIX "/display" BASE "../os/stage-osdos/03-boot/files"
                     FILES "../os/stage-osdos/03-boot/files/display-presets.json")
    # The same as a Pi 5, whose outputs are others.
    add_test(NAME display_output_pi5 COMMAND display_output_test)
    set_tests_properties(display_output_pi5 PROPERTIES
        ENVIRONMENT "OSDOS_BOARD_MODEL=Raspberry Pi 5 Model B Rev 1.0")
    # And as a Pi 3, whose DPI is a Pi 4's: the same outputs, its AV jack too.
    add_test(NAME display_output_pi3 COMMAND display_output_test)
    set_tests_properties(display_output_pi3 PROPERTIES
        ENVIRONMENT "OSDOS_BOARD_MODEL=Raspberry Pi 3 Model B Plus Rev 1.3")
    # And as a Pi 500, a Pi 5 without the TV pads' composite.
    add_test(NAME display_output_pi500 COMMAND display_output_test)
    set_tests_properties(display_output_pi500 PROPERTIES
        ENVIRONMENT "OSDOS_BOARD_MODEL=Raspberry Pi 500 Rev 1.0")

    # The OSD/OS image's boot screen (BootProgress) against a stand-in for
    # systemctl: the services it lists as they start, and OSD/OS Setup's
    # SETTINGS only once it does.
    osdos_cpp_test(boot_progress SOURCES ../src/boot/BootProgress.cpp LIBRARIES osdos_test_names)

    # Settings → Network → Bluetooth's DETAILS (BluetoothManager, built
    # without BlueZ, as on a Mac) against a stand-in for journalctl: the
    # system log's Bluetooth lines, one reading at a time, and one that
    # takes too long stopped with what it gave by then.
    osdos_cpp_test(bluetooth SOURCES ../src/bluetooth/BluetoothManager.cpp)

    # A process over, told once each time (util/ProcessOver.h), against
    # /bin/sh: one that exits, one killed, one that can't be started, one
    # started again, and one still running at the time limit, told at once,
    # then ended and deleted.
    osdos_cpp_test(process_over)

    # Settings → System → System Information (SystemInfo): its rows from what
    # the system says, Linux's files from a stand-in root, the name a network
    # takes of what is typed, and the device renamed through a stand-in for
    # osdos-name.socket.
    osdos_cpp_test(system_info SOURCES ../src/system/SystemInfo.cpp LIBRARIES osdos_test_system Qt6::Network)

    # The system around an arcade game (GameSystem) against a stand-in for
    # osdos-game.socket: what a game's start sends and its end, the
    # connection held between, the line of what the Pi did and the notice,
    # and nothing asked without the helper.
    osdos_cpp_test(game_system SOURCES ../src/modules/arcade/GameSystem.cpp
        LIBRARIES osdos_test_system osdos_test_core Qt6::Network)
    # The same as a Raspberry Pi 4, where a game without the helper says so.
    add_test(NAME game_system_pi4 COMMAND game_system_test)
    set_tests_properties(game_system_pi4 PROPERTIES ENVIRONMENT "OSDOS_BOARD_MODEL=Raspberry Pi 4 Model B Rev 1.5")

    # Settings → System's Time Zone and Keyboard (SystemSettings): what the
    # system has from a stand-in root, the lists it offers, and a new one sent
    # through a stand-in for osdos-system.socket.
    osdos_cpp_test(system_settings SOURCES ../src/system/SystemSettings.cpp
        DEFINITIONS SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/.." LIBRARIES osdos_test_system Qt6::Network)

    # Settings → Network → Wi-Fi (WifiManager) against a stand-in for NetworkManager
    # (fake_networkmanager) on a D-Bus of the test's own, and for the country
    # against a stand-in boot partition and osdos-wifi.socket. Built where Qt
    # has D-Bus, as the app's Wi-Fi is; the test skips itself without
    # dbus-daemon. The fake_networkmanager program is the stand-in on its own,
    # for screenshots of the app against it; ctest doesn't run it.
    find_package(Qt6 QUIET COMPONENTS DBus)
    if(Qt6DBus_FOUND)
        osdos_cpp_test(wifi SOURCES fake_networkmanager.cpp ../src/wifi/WifiManager.cpp
            DEFINITIONS OSDOS_WIFI=1 LIBRARIES osdos_test_system Qt6::DBus Qt6::Network)

        add_executable(fake_networkmanager fake_networkmanager_main.cpp fake_networkmanager.cpp)
        target_link_libraries(fake_networkmanager PRIVATE Qt6::Core Qt6::DBus)
    else()
        message(WARNING "Qt D-Bus not found: the Wi-Fi test is left out")
    endif()

    # The Arcade module against a stand-in for MAME (a shell script): what
    # MAME says of the games folder's sets and the tree made of it, a game
    # started with the players' controllers and a way out, and how one that
    # didn't play ended.
    add_executable(arcade_test
        arcade_test.cpp
        ../src/modules/arcade/ArcadeBackend.cpp
        ../src/modules/arcade/ArcadePictures.cpp
        ../src/modules/arcade/LanRooms.cpp
        ../src/modules/arcade/Mame.cpp
        ../src/modules/arcade/Libretro.cpp
        ../src/modules/arcade/FbneoInstaller.cpp
        ../src/modules/arcade/GameSystem.cpp
        ../src/modules/arcade/ProgramInstaller.cpp
        ../src/modules/arcade/RetroAchievements.cpp
        ../src/modules/arcade/RetroAchievementsAccount.cpp
        ../src/modules/arcade/ArcadeDb.cpp
        ../src/modules/arcade/RomCheck.cpp
        ../src/util/SevenZip.cpp)
    target_include_directories(arcade_test PRIVATE ../src ${Libdrm_INCLUDE_DIRS})
    target_compile_definitions(arcade_test PRIVATE TESTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    osdos_engine_definitions(arcade_test)
    target_link_libraries(arcade_test PRIVATE osdos_test_launch osdos_test_system
        Qt6::Core Qt6::Concurrent Qt6::Network Qt6::Qml Qt6::Test ${Libdrm_LIBRARIES} osdos_lzma)
    # FinalBurn Neo's download unpacks its core with minizip, where there is one.
    if(OPENHI2TXT_FOUND)
        target_compile_definitions(arcade_test PRIVATE OSDOS_MINIZIP=1)
        target_include_directories(arcade_test PRIVATE ${MINIZIP_INCLUDE_DIR})
        target_link_libraries(arcade_test PRIVATE ${MINIZIP_LIBRARY})
    endif()
    add_test(NAME arcade COMMAND arcade_test)
    # Its stand-ins for MAME and RetroArch run as processes and its timers
    # for real, test after test: close to a minute in all, the minute CI
    # gives a test without a timeout of its own.
    set_tests_properties(arcade PROPERTIES TIMEOUT 180)
    # A libretro core that only describes itself, for libretro::loadCoreInfo().
    enable_language(C)
    add_library(arcade_test_core MODULE arcade_test_core.c)
    set_target_properties(arcade_test_core PROPERTIES PREFIX "" AUTOMOC OFF)
    add_dependencies(arcade_test arcade_test_core)
    target_compile_definitions(arcade_test PRIVATE TEST_CORE="$<TARGET_FILE:arcade_test_core>")

    # A playlist's m3u: owner-only and whole, the one before removed once it
    # is written, and one a full card can't take reported, the one before
    # kept. Linux only, as the arcade test, for what the backends start.
    osdos_cpp_test(playlists
        SOURCES ../src/modules/playlists/PlaylistsBackend.cpp ../src/modules/playlists/ServerDownload.cpp
                ../src/modules/local_files/LocalFilesBackend.cpp ../src/modules/local_files/RemovableDrives.cpp
        LIBRARIES osdos_test_youtube Qt6::Concurrent Qt6::Network Qt6::Qml)

    # YouTube's subscriptions read from a stand-in for its feeds: four at a
    # time, the channel opened first. Linux only, as the playlists test, for
    # the sign-in browser the backend holds.
    osdos_cpp_test(youtube LIBRARIES osdos_test_youtube Qt6::Network Qt6::Qml)

    # A Raspberry Pi to JAMMA board's pins as gamepad events (osdos-jamma).
    # Linux only, as uinput's codes are.
    osdos_cpp_test(jamma SOURCES ../src/jamma/JammaPanel.cpp)

    # osdos-modetest (src/modetest): modelines read, the built-in modes, which
    # a screen may be given and the picture drawn in each; and the tool itself
    # where there is no screen: a modeline that isn't one turned down, and
    # none found to test on. Linux only, as libdrm is.
    osdos_cpp_test(modetest SOURCES ../src/modetest/ModeLine.cpp)
    add_executable(modetest_tool ../src/modetest/main.cpp ../src/modetest/ModeLine.cpp)
    set_target_properties(modetest_tool PROPERTIES AUTOMOC OFF)
    target_include_directories(modetest_tool PRIVATE ${Libdrm_INCLUDE_DIRS})
    target_link_libraries(modetest_tool PRIVATE ${Libdrm_LIBRARIES})
    add_test(NAME modetest_tool_modeline COMMAND modetest_tool --mode "6.5 320 300 364 416 224 236 239 265")
    set_tests_properties(modetest_tool_modeline PROPERTIES
        PASS_REGULAR_EXPRESSION "not a modeline \\(the horizontal sync isn't in the blanking after the picture\\)")
    add_test(NAME modetest_tool_no_screen COMMAND modetest_tool)
    set_tests_properties(modetest_tool_no_screen PROPERTIES
        ENVIRONMENT "OSDOS_MODETEST_DRI=${CMAKE_CURRENT_BINARY_DIR}/no-dri"
        PASS_REGULAR_EXPRESSION "no screen to test on: no DPI output is showing a picture")

    # A release's notes (scripts/release-notes.sh) on a made-up history: the
    # commits since the previous tag under What's New, Changelog and Bug Fixes.
    add_test(NAME release_notes COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/release_notes_test.sh
                                        ${CMAKE_CURRENT_SOURCE_DIR}/../scripts/release-notes.sh)

    # The Downloads table at the end of the notes (.github/workflows/release.yml):
    # a row per file published, in the order the release page lists the files.
    add_test(NAME release_downloads COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/release_downloads_test.sh
                                            ${CMAKE_CURRENT_SOURCE_DIR}/../.github/workflows/release.yml)

    # The logs' copy to the card for Quit → Save Logs on the image
    # (scripts/save-logs.sh), on a stand-in partition and journalctl.
    add_test(NAME save_logs COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/save_logs_test.sh
                                     ${CMAKE_CURRENT_SOURCE_DIR}/../scripts/save-logs.sh)

    # FinalBurn Neo's source unpacked and both cores built in order
    # (scripts/build-fbneo-cores.sh), with stand-ins for curl and the builds.
    add_test(NAME fbneo_cores COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/fbneo_cores_test.sh
                                       ${CMAKE_CURRENT_SOURCE_DIR}/../scripts/build-fbneo-cores.sh)

    # The image's stop helper, osdos-stop (scripts/install.sh), on a stand-in
    # boot partition: the display presets it writes, and At Next Start's and
    # the Wi-Fi country's requests, good and bad.
    add_test(NAME osdos_stop COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/osdos_stop_test.sh
                                     ${CMAKE_CURRENT_SOURCE_DIR}/../scripts/install.sh
                                     ${CMAKE_CURRENT_SOURCE_DIR}/../os/stage-osdos/03-boot/files)

    # The image's marking of a network game's packets for Wi-Fi's video
    # queue (osdos-netplay-priority): the port against the Arcade module's,
    # the addresses, the unit and its place in the stage, and the rules given
    # to nft where it is here and root.
    add_test(NAME netplay_priority COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/netplay_priority_test.sh
                                           ${CMAKE_CURRENT_SOURCE_DIR}/../os/stage-osdos/02-system
                                           ${CMAKE_CURRENT_SOURCE_DIR}/../src/modules/arcade/Libretro.h)

    # OSD/OS Setup's file applied as the card starts (osdos-setup, the
    # image's), on a stand-in card with the image's osdos-stop: a whole file,
    # the same again, parts it refuses, files it can't read. Needs Python 3.
    add_test(NAME osdos_setup COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/osdos_setup_test.sh
                                      ${CMAKE_CURRENT_SOURCE_DIR}/../os/stage-osdos/02-system/files/osdos-setup
                                      ${CMAKE_CURRENT_SOURCE_DIR}/../scripts/install.sh
                                      ${CMAKE_CURRENT_SOURCE_DIR}/../os/stage-osdos/03-boot/files)

    # The launcher (scripts/install.sh) on a stand-in install: a staged update
    # applied, or one cut short by a power cut put right, and a Pi 5 waiting
    # for its display preset's output.
    add_test(NAME launcher COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/launcher_test.sh
                                   ${CMAKE_CURRENT_SOURCE_DIR}/../scripts/install.sh)

    # The installer's choice of the user OSD/OS runs as (scripts/install.sh):
    # no service yet, one installed before, and the image's own.
    add_test(NAME installer COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/installer_test.sh
                                    ${CMAKE_CURRENT_SOURCE_DIR}/../scripts/install.sh)

    # The image's packages (os/stage-osdos/00-packages) are the installer's,
    # less qt6-wayland and plus alsa-utils.
    add_test(NAME image_packages COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/image_packages_test.sh
                                         ${CMAKE_CURRENT_SOURCE_DIR}/../scripts/install.sh
                                         ${CMAKE_CURRENT_SOURCE_DIR}/../os/stage-osdos/00-packages/00-packages-nr)
endif()

# The runner's engine has the app's provider of the pixel symbols' pictures
# (PixelIcon, image://osdpixel), as main.cpp's has.
add_executable(tree_browser_test tree_browser_test.cpp ../src/util/OsdIconProvider.cpp)
target_include_directories(tree_browser_test PRIVATE ../src)
target_link_libraries(tree_browser_test PRIVATE Qt6::QuickTest Qt6::Quick)
# A QML test (qml/<file>), run by tree_browser's runner offscreen with the
# software renderer, the views' components (views/Components, as `import
# Components`) and the stand-ins in stubs/ (the app's VhsNoise, Main.qml's
# root) to import.
function(osdos_qml_test name qml)
    add_test(NAME ${name} COMMAND tree_browser_test -input ${CMAKE_CURRENT_SOURCE_DIR}/qml/${qml}
                                  -import ${CMAKE_CURRENT_SOURCE_DIR}/../views
                                  -import ${CMAKE_CURRENT_SOURCE_DIR}/stubs)
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
endfunction()
osdos_qml_test(tree_browser tst_tree_browser.qml)
# Settings → Controls' pages (views/Controllers.qml, ControllerSettings.qml,
# MenuButtons.qml, ControllerTest.qml, ArcadeButtons.qml) against a stand-in
# for InputManager, with the same QML test runner.
osdos_qml_test(controls_views tst_controllers.qml)
# The Arcade module's views (modules/arcade/views/Games.qml, ArcadeScreen.qml,
# Play.qml) against a stand-in for ArcadeBackend.
osdos_qml_test(arcade_views tst_arcade.qml)
# Its views' own waits run for real (a held select, Attract Mode's idle time,
# a name's scroll), view after view: a minute in all, the minute CI gives a
# test without a timeout of its own.
set_tests_properties(arcade_views PROPERTIES TIMEOUT 180)
# A module's settings page (views/ModuleSettings.qml) against a stand-in for
# AppCore: rows that follow another row's value.
osdos_qml_test(module_settings_view tst_module_settings.qml)
# Settings (views/Settings.qml): the first screen's groups and the general
# settings, and TV / Movie's page, against stand-ins for AppCore and the rest;
# and the title bar (AppBar) with a title too long for it.
osdos_qml_test(settings_view tst_settings.qml)
# The main menu (views/ModuleList.qml) against a stand-in for AppCore: the
# modules' rows, and Menu Layout's Camcorder, the rows in groups beside a
# column of icons.
osdos_qml_test(main_menu_view tst_main_menu.qml)
# A view's menu (views/Components/MenuList.qml) on its own: the rows its
# cursor and the mouse stop on, round the ends, and the cursor held for the
# host.
osdos_qml_test(menu_list_view tst_menu_list.qml)
# A line too long for its room scrolled through (views/Components/ScrolledText.qml) on its own.
osdos_qml_test(scrolled_text tst_scrolled_text.qml)
# Settings → Network → Wi-Fi (views/Wifi.qml) against a stand-in for WifiManager: the
# switch, the country and its chooser, joining with a password, a network's
# options, one not listed, and a search.
osdos_qml_test(wifi_views tst_wifi.qml)
# Settings → Network → Bluetooth (views/Bluetooth.qml) against a stand-in for
# BluetoothManager: its lines, a device's options, and the lines changed where
# they are as BlueZ says more.
osdos_qml_test(bluetooth_views tst_bluetooth.qml)
# What comes over a view, each on its own: the on-screen keyboard
# (views/Components/OnScreenKeyboard.qml), the choice window
# (views/Components/ChoiceOverlay.qml) and the screen saver
# (views/Components/ScreenSaver.qml).
osdos_qml_test(overlays_views tst_overlays.qml)
# The window and its changes (views/Components/WindowChange.qml), against a
# stand-in for Main.qml: when a view changes at once and when with its
# transition, and the cube's turn.
osdos_qml_test(window_change tst_window_change.qml)
# A module's router (views/Components/ModuleRouter.qml), as a module's
# Root.qml is one (qml/module_router/), against a stand-in for Main.qml: the
# way forward and back, the two replaces, a view the Root opens its own way,
# inFrame, and the shell's own navigateTo.
osdos_qml_test(module_router tst_module_router.qml)
# The catalogue browsers, YouTube's (modules/youtube/views/Items.qml) and a
# web player module's (views/Components/WebPlayerBrowse.qml), both a
# CatalogBrowser (views/Components/CatalogBrowser.qml), against
# stand-ins for their backends.
osdos_qml_test(catalog_browser_views tst_catalog_browsers.qml)

# Settings → Look → Menu Layout's monitor layouts as a frame every kind of
# view takes its place in (views/Components/MonitorChrome.qml and the shared
# components that hand it their content), against a stand-in for Main.qml.
osdos_qml_test(monitor_frame_views tst_monitor_frame.qml)
# About (views/About.qml): OSD/OS's lines and the arcade's, each license a
# page of its own, against stand-ins for the license files.
osdos_qml_test(about_view tst_about.qml)
# Settings → Screen → Test Patterns (views/TestPatterns.qml): every pattern there and
# loading, the keys through them, the name and keys over them, the effects
# resting and the screen saver waiting.
osdos_qml_test(test_patterns_view tst_test_patterns.qml)
# Settings → System → System Information (views/SystemInfo.qml): its rows, the cursor kept on its
# line as they are read again, and the device renamed on the keyboard, against
# a stand-in for SystemInfo.
osdos_qml_test(system_info_view tst_system_info.qml)
# The High Scores module's view (modules/highscores/views/Scores.qml) against
# a stand-in for HighScoresBackend.
osdos_qml_test(highscores_views tst_highscores.qml)
# The media server modules' players, detail screens and routers (Plex,
# Jellyfin, Emby) and Local Files' player against stand-ins for their
# backends and MpvController, and for the app's VhsNoise (stubs).
osdos_qml_test(media_players_views tst_media_players.qml)

# Settings → Controls, each controller apart: the labels, the profiles, a
# pad's Arcade Buttons as MAME and RetroArch name them and whose a key is
# (ControllerProfile), and InputManager with pads SDL makes up (its virtual
# joysticks), its settings kept in config.json (AppCore).
# Its waits are InputManager's own, for real (a hold's second and a half,
# the unlock buttons' three seconds, a page's wait run out): a minute and
# more in all, past the minute CI gives a test without a timeout of its own.
find_package(SDL2 CONFIG REQUIRED)
osdos_cpp_test(controllers
    SOURCES ../src/input/InputManager.cpp ../src/input/ControllerProfile.cpp ../src/input/JammaPins.cpp
            ../src/input/IdleTracker.cpp
    LIBRARIES osdos_test_core Qt6::Gui Qt6::Quick Qt6::Qml Qt6::Network SDL2::SDL2
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 180)

# The app itself (the build in build/, made apart from the tests; OSDOS_APP
# for another), started offscreen: the main menu with every module on it,
# each module as the startup module, and OSD/OS Setup's notice for a file in
# the data folder open without a QML warning, and the app stops on SIGTERM as
# the service stops it. Skipped without the app, and for a
# module that imports a QML module of Qt's this Qt doesn't have.
if(UNIX)
    if(APPLE)
        set(osdos_app "${CMAKE_CURRENT_SOURCE_DIR}/../build/osdos.app/Contents/MacOS/osdos")
    else()
        set(osdos_app "${CMAKE_CURRENT_SOURCE_DIR}/../build/osdos")
    endif()
    set(OSDOS_APP "${osdos_app}" CACHE FILEPATH "The app the smoke tests start (app_smoke_test.sh)")
    get_filename_component(osdos_root "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
    set(qt_qml "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_QML}")
    set(smoke ${CMAKE_CURRENT_SOURCE_DIR}/app_smoke_test.sh ${OSDOS_APP} ${osdos_root} ${qt_qml})
    add_test(NAME app_main_menu COMMAND bash ${smoke})
    # OSD/OS Setup's file in the data folder, taken as the app starts.
    add_test(NAME app_setup_file COMMAND bash ${smoke} --setup)
    # The main menu in each of Settings → Look → Menu Layout's other layouts.
    add_test(NAME app_main_menu_camcorder COMMAND bash ${smoke} --layout=Camcorder)
    add_test(NAME app_main_menu_studio_monitor COMMAND bash ${smoke} "--layout=Studio Monitor")
    add_test(NAME app_main_menu_broadcast COMMAND bash ${smoke} --layout=Broadcast)
    add_test(NAME app_main_menu_flat_panel COMMAND bash ${smoke} "--layout=Flat Panel")
    # The Arcade screen on a monitor turned on its side (Settings → Screen →
    # Monitor), in its tall layout.
    add_test(NAME app_arcade_turned_right COMMAND bash ${smoke} "--monitor=Turned Right")
    add_test(NAME app_arcade_turned_left COMMAND bash ${smoke} "--monitor=Turned Left")
    # Its first frame, with a theme chosen: on the theme's ground.
    add_test(NAME app_first_frame
             COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/first_frame_test.sh ${OSDOS_APP} ${osdos_root})
    set(smoke_tests app_main_menu app_setup_file app_main_menu_camcorder app_main_menu_studio_monitor
                    app_main_menu_broadcast app_main_menu_flat_panel app_arcade_turned_right app_arcade_turned_left
                    app_first_frame)
    file(GLOB manifests CONFIGURE_DEPENDS ${osdos_root}/modules/*/manifest.json)
    foreach(manifest ${manifests})
        get_filename_component(folder ${manifest} DIRECTORY)
        get_filename_component(folder ${folder} NAME)
        add_test(NAME app_module_${folder} COMMAND bash ${smoke} ${folder})
        list(APPEND smoke_tests app_module_${folder})
        # And in each monitor layout, whose frame the shell keeps round a
        # module's views (Main.qml's frameChrome).
        add_test(NAME app_module_${folder}_studio_monitor COMMAND bash ${smoke} ${folder} "--layout=Studio Monitor")
        add_test(NAME app_module_${folder}_broadcast COMMAND bash ${smoke} ${folder} --layout=Broadcast)
        add_test(NAME app_module_${folder}_flat_panel COMMAND bash ${smoke} ${folder} "--layout=Flat Panel")
        list(APPEND smoke_tests app_module_${folder}_studio_monitor app_module_${folder}_broadcast
                                app_module_${folder}_flat_panel)
    endforeach()
    # And asked a libretro core's name and version, as the Arcade module
    # asks a copy of it (--core-info), the test core's, which is built with
    # the arcade test (Linux).
    if(TARGET arcade_test_core)
        add_test(NAME app_core_info
                 COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/app_core_info_test.sh ${OSDOS_APP} $<TARGET_FILE:arcade_test_core>)
        list(APPEND smoke_tests app_core_info)
    endif()
    set_tests_properties(${smoke_tests} PROPERTIES SKIP_RETURN_CODE 77)
endif()
