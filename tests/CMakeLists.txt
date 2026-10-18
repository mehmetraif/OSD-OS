cmake_minimum_required(VERSION 3.21)
project(osdos_regression_tests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Core Concurrent Network Qml Test)
enable_testing()
link_libraries(Qt6::Concurrent)
add_executable(reliability_test
    reliability_test.cpp
    ../src/modules/playlists/ServerDownload.cpp
    ../src/modules/weather/WeatherBackend.cpp
    ../src/update/UpdateManager.cpp
    ../src/AppCore.cpp
    ../src/audio/AudioOutput.cpp
    ../src/audio/MenuMusic.cpp
    ../src/util/LegacyNames.cpp
    ../src/util/MpvLocator.cpp)
target_include_directories(reliability_test PRIVATE ../src)
target_compile_definitions(reliability_test PRIVATE APP_VERSION="test")
target_link_libraries(reliability_test PRIVATE Qt6::Core Qt6::Network Qt6::Qml Qt6::Test)
add_test(NAME reliability COMMAND reliability_test)
find_program(LUA_EXECUTABLE NAMES lua5.4 lua REQUIRED)
add_test(NAME osc_controls COMMAND ${LUA_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/osc_test.lua
                                  ${CMAKE_CURRENT_SOURCE_DIR}/../scripts/mpv-osc.lua)
add_executable(storage_search_test
    storage_search_test.cpp
    ../src/AppCore.cpp
    ../src/util/LegacyNames.cpp
    ../src/modules/local_files/LocalFilesBackend.cpp
    ../src/modules/local_files/RemovableDrives.cpp)
target_include_directories(storage_search_test PRIVATE ../src)
target_link_libraries(storage_search_test PRIVATE
    Qt6::Core Qt6::Concurrent Qt6::Network Qt6::Qml Qt6::Test)
add_test(NAME storage_search COMMAND storage_search_test)

# Themes and skins, read from the app's folder and the data folder's (AppCore).
add_executable(looks_test
    looks_test.cpp
    ../src/AppCore.cpp
    ../src/util/LegacyNames.cpp)
target_include_directories(looks_test PRIVATE ../src)
target_link_libraries(looks_test PRIVATE Qt6::Core Qt6::Network Qt6::Qml Qt6::Test)
add_test(NAME looks COMMAND looks_test)

# The Jellyfin, Emby and Plex backends against a stand-in network: a list
# asked for after another drops the first's reply, lists kept until a playback
# ends, a stream given up dropped unreported, an auth check that changes
# nothing quiet, and Plex's next item for a player left dropped.
find_package(OpenSSL REQUIRED)
add_executable(media_servers_test
    media_servers_test.cpp
    ../src/modules/jellyfin/JellyfinBackend.cpp
    ../src/modules/emby/EmbyBackend.cpp
    ../src/modules/plex/PlexBackend.cpp)
target_include_directories(media_servers_test PRIVATE ../src)
target_link_libraries(media_servers_test PRIVATE Qt6::Core Qt6::Concurrent Qt6::Network Qt6::Test OpenSSL::Crypto)
add_test(NAME media_servers COMMAND media_servers_test)

# An NFC card's file: mapped again, the old one kept till the new one is
# written, and a long title cut to a name every filesystem takes.
add_executable(nfc_cards_test
    nfc_cards_test.cpp
    ../src/modules/nfc_reader/NfcReaderBackend.cpp
    ../src/modules/nfc_reader/PcscDriver.cpp
    ../src/modules/nfc_reader/Pn532SerialDriver.cpp
    ../src/modules/nfc_reader/SerialPort.cpp
    ../src/AppCore.cpp
    ../src/util/LegacyNames.cpp)
target_include_directories(nfc_cards_test PRIVATE ../src)
target_link_libraries(nfc_cards_test PRIVATE Qt6::Core Qt6::Network Qt6::Qml Qt6::Test)
add_test(NAME nfc_cards COMMAND nfc_cards_test)

# The High Scores module on MAME's files of three games (data/highscores),
# read by openhi2txt with hi2txt's definitions: the tables as lines, what is
# kept and read again, the games' names, the files that can't be read. Where
# minizip is found, as the module's tables are in the app.
include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/openhi2txt.cmake)
if(OPENHI2TXT_FOUND)
    add_executable(highscores_test
        highscores_test.cpp
        ../src/modules/highscores/HighScoresBackend.cpp
        ../src/modules/highscores/ScoreTable.cpp
        ../src/modules/arcade/Mame.cpp)
    target_include_directories(highscores_test PRIVATE ../src)
    target_compile_definitions(highscores_test PRIVATE
        OSDOS_HIGH_SCORES=1 APP_DIR="${CMAKE_CURRENT_SOURCE_DIR}/..")
    target_link_libraries(highscores_test PRIVATE Qt6::Core Qt6::Test openhi2txt::openhi2txt)
    add_test(NAME highscores COMMAND highscores_test)
else()
    message(WARNING "minizip not found: the High Scores test is left out")
endif()

# One video handed over to the next (MpvController), against a stand-in for
# mpv. Linux only: the stand-in is a shell script that looks in /proc.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(Qt6 REQUIRED COMPONENTS Gui Quick)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(Libdrm REQUIRED libdrm)
    add_executable(playback_retire_test
        playback_retire_test.cpp
        ../src/player/MpvController.cpp
        ../src/player/EmbeddedMpv.cpp
        ../src/player/VideoSurface.cpp
        ../src/util/Diagnostics.cpp
        ../src/AppCore.cpp
        ../src/audio/AudioOutput.cpp
        ../src/util/Board.cpp
        ../src/util/DisplayHandoff.cpp
        ../src/util/ExecPath.cpp
        ../src/util/FontconfigOverride.cpp
        ../src/util/LegacyNames.cpp
        ../src/util/MpvLocator.cpp
        ../src/util/YtDlpLocator.cpp
        ../src/audio/MenuMusic.cpp)
    target_include_directories(playback_retire_test PRIVATE ../src ${Libdrm_INCLUDE_DIRS})
    target_link_libraries(playback_retire_test PRIVATE
        Qt6::Core Qt6::Gui Qt6::Quick Qt6::Qml Qt6::Network Qt6::Test ${Libdrm_LIBRARIES})
    add_test(NAME playback_retire COMMAND playback_retire_test)

    # The menu music, against a stand-in for mpv: it plays in the menus, and
    # stops at once when anything else is about to play sound.
    add_executable(menu_music_test
        menu_music_test.cpp
        ../src/audio/MenuMusic.cpp
        ../src/audio/AudioOutput.cpp
        ../src/AppCore.cpp
        ../src/util/LegacyNames.cpp
        ../src/util/MpvLocator.cpp)
    target_include_directories(menu_music_test PRIVATE ../src)
    target_link_libraries(menu_music_test PRIVATE Qt6::Core Qt6::Network Qt6::Qml Qt6::Test)
    add_test(NAME menu_music COMMAND menu_music_test)

    # The crash and stall reports (util/Diagnostics), from child processes
    # that crash or freeze on purpose. Linux only, as the reports are.
    add_executable(diagnostics_test diagnostics_test.cpp ../src/util/Diagnostics.cpp)
    target_include_directories(diagnostics_test PRIVATE ../src)
    target_link_libraries(diagnostics_test PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME diagnostics COMMAND diagnostics_test)

    # Settings → Display Output against the image's presets in a stand-in boot
    # partition: which are offered, by board and launcher API, and At Next
    # Start through a stand-in for its socket.
    add_executable(display_output_test
        display_output_test.cpp
        ../src/display/DisplayOutput.cpp
        ../src/util/Board.cpp
        ../src/util/LegacyNames.cpp)
    target_include_directories(display_output_test PRIVATE ../src)
    target_compile_definitions(display_output_test PRIVATE
        PRESETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../os/stage-osdos/03-boot/files"
        INSTALL_SH="${CMAKE_CURRENT_SOURCE_DIR}/../scripts/install.sh")
    target_link_libraries(display_output_test PRIVATE Qt6::Core Qt6::Network Qt6::Test)
    add_test(NAME display_output COMMAND display_output_test)
    # The same as a Pi 5, whose outputs are others.
    add_test(NAME display_output_pi5 COMMAND display_output_test)
    set_tests_properties(display_output_pi5 PROPERTIES
        ENVIRONMENT "OSDOS_BOARD_MODEL=Raspberry Pi 5 Model B Rev 1.0")

    # Settings → Wi-Fi (WifiManager) against a stand-in for NetworkManager
    # (fake_networkmanager) on a D-Bus of the test's own, and for the country
    # against a stand-in boot partition and osdos-wifi.socket. Built where Qt
    # has D-Bus, as the app's Wi-Fi is; the test skips itself without
    # dbus-daemon. The fake_networkmanager program is the stand-in on its own,
    # for screenshots of the app against it; ctest doesn't run it.
    find_package(Qt6 QUIET COMPONENTS DBus)
    if(Qt6DBus_FOUND)
        add_executable(wifi_test
            wifi_test.cpp
            fake_networkmanager.cpp
            ../src/wifi/WifiManager.cpp
            ../src/util/LegacyNames.cpp)
        target_include_directories(wifi_test PRIVATE ../src)
        target_compile_definitions(wifi_test PRIVATE OSDOS_WIFI=1)
        target_link_libraries(wifi_test PRIVATE Qt6::Core Qt6::DBus Qt6::Network Qt6::Test)
        add_test(NAME wifi COMMAND wifi_test)

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
        ../src/modules/arcade/Mame.cpp
        ../src/modules/arcade/Libretro.cpp
        ../src/modules/arcade/FbneoInstaller.cpp
        ../src/modules/arcade/RetroAchievements.cpp
        ../src/modules/scripts/ScriptLauncher.cpp
        ../src/input/ControllerProfile.cpp
        ../src/audio/AudioOutput.cpp
        ../src/audio/MenuMusic.cpp
        ../src/util/DisplayHandoff.cpp
        ../src/util/MpvLocator.cpp
        ../src/AppCore.cpp
        ../src/util/LegacyNames.cpp)
    target_include_directories(arcade_test PRIVATE ../src ${Libdrm_INCLUDE_DIRS})
    target_compile_definitions(arcade_test PRIVATE TESTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(arcade_test PRIVATE
        Qt6::Core Qt6::Network Qt6::Qml Qt6::Test ${Libdrm_LIBRARIES})
    # FinalBurn Neo's download unpacks its core with minizip, where there is one.
    if(OPENHI2TXT_FOUND)
        target_compile_definitions(arcade_test PRIVATE OSDOS_MINIZIP=1)
        target_include_directories(arcade_test PRIVATE ${MINIZIP_INCLUDE_DIR})
        target_link_libraries(arcade_test PRIVATE ${MINIZIP_LIBRARY})
    endif()
    add_test(NAME arcade COMMAND arcade_test)

    # A playlist's m3u: owner-only and whole, the one before removed once it
    # is written, and one a full card can't take reported, the one before
    # kept. Linux only, as the arcade test, for what the backends start.
    add_executable(playlists_test
        playlists_test.cpp
        ../src/modules/playlists/PlaylistsBackend.cpp
        ../src/modules/playlists/ServerDownload.cpp
        ../src/modules/local_files/LocalFilesBackend.cpp
        ../src/modules/local_files/RemovableDrives.cpp
        ../src/modules/youtube/YouTubeBackend.cpp
        ../src/modules/web_player/WebPlayerBackend.cpp
        ../src/modules/web_player/TmdbCatalog.cpp
        ../src/modules/scripts/ScriptLauncher.cpp
        ../src/input/ControllerProfile.cpp
        ../src/audio/AudioOutput.cpp
        ../src/audio/MenuMusic.cpp
        ../src/util/DisplayHandoff.cpp
        ../src/util/MpvLocator.cpp
        ../src/util/YtDlpLocator.cpp
        ../src/AppCore.cpp
        ../src/util/LegacyNames.cpp)
    target_include_directories(playlists_test PRIVATE ../src ${Libdrm_INCLUDE_DIRS})
    target_link_libraries(playlists_test PRIVATE
        Qt6::Core Qt6::Network Qt6::Qml Qt6::Test ${Libdrm_LIBRARIES})
    add_test(NAME playlists COMMAND playlists_test)

    # A Raspberry Pi to JAMMA board's pins as gamepad events (osdos-jamma).
    # Linux only, as uinput's codes are.
    add_executable(jamma_test jamma_test.cpp ../src/jamma/JammaPanel.cpp)
    target_include_directories(jamma_test PRIVATE ../src)
    target_link_libraries(jamma_test PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME jamma COMMAND jamma_test)

    # A release's notes (scripts/release-notes.sh) on a made-up history: the
    # commits since the previous tag under What's New, Changelog and Bug Fixes.
    add_test(NAME release_notes COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/release_notes_test.sh
                                        ${CMAKE_CURRENT_SOURCE_DIR}/../scripts/release-notes.sh)

    # The image's stop helper, osdos-stop (scripts/install.sh), on a stand-in
    # boot partition: the display presets it writes, and At Next Start's
    # requests, good and bad.
    add_test(NAME osdos_stop COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/osdos_stop_test.sh
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
endif()

find_package(Qt6 REQUIRED COMPONENTS Gui Quick QuickTest)
add_executable(tree_browser_test tree_browser_test.cpp)
target_link_libraries(tree_browser_test PRIVATE Qt6::QuickTest Qt6::Quick)
add_test(NAME tree_browser COMMAND tree_browser_test -input ${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_tree_browser.qml)
set_tests_properties(tree_browser PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
# Settings → Controls' pages (views/Controllers.qml, ControllerSettings.qml,
# RemapControls.qml) against a stand-in for InputManager, with the same QML
# test runner.
add_test(NAME controls_views COMMAND tree_browser_test -input ${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_controllers.qml
                                     -import ${CMAKE_CURRENT_SOURCE_DIR}/../views)
set_tests_properties(controls_views PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
# The Arcade module's views (modules/arcade/views/Games.qml, ArcadeScreen.qml,
# Play.qml) against a stand-in for ArcadeBackend.
add_test(NAME arcade_views COMMAND tree_browser_test -input ${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_arcade.qml
                                   -import ${CMAKE_CURRENT_SOURCE_DIR}/../views)
set_tests_properties(arcade_views PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
# A module's settings page (views/ModuleSettings.qml) against a stand-in for
# AppCore: rows that follow another row's value.
add_test(NAME module_settings_view COMMAND tree_browser_test -input ${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_module_settings.qml
                                           -import ${CMAKE_CURRENT_SOURCE_DIR}/../views)
set_tests_properties(module_settings_view PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
# Settings (views/Settings.qml): the first screen's groups and the general
# settings, and TV / Movie's page, against stand-ins for AppCore and the rest;
# and the title bar (AppBar) with a title too long for it.
add_test(NAME settings_view COMMAND tree_browser_test -input ${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_settings.qml
                                    -import ${CMAKE_CURRENT_SOURCE_DIR}/../views)
set_tests_properties(settings_view PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
# Settings → Wi-Fi (views/Wifi.qml) against a stand-in for WifiManager: the
# switch, the country and its chooser, joining with a password, a network's
# options, one not listed, and a search.
add_test(NAME wifi_views COMMAND tree_browser_test -input ${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_wifi.qml
                                 -import ${CMAKE_CURRENT_SOURCE_DIR}/../views)
set_tests_properties(wifi_views PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
# About (views/About.qml): OSD/OS's lines and the arcade's, each licence a
# page of its own, against stand-ins for the licence files.
add_test(NAME about_view COMMAND tree_browser_test -input ${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_about.qml
                                 -import ${CMAKE_CURRENT_SOURCE_DIR}/../views)
set_tests_properties(about_view PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
# The High Scores module's view (modules/highscores/views/Scores.qml) against
# a stand-in for HighScoresBackend.
add_test(NAME highscores_views COMMAND tree_browser_test -input ${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_highscores.qml
                                       -import ${CMAKE_CURRENT_SOURCE_DIR}/../views)
set_tests_properties(highscores_views PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
# The media server modules' players, detail screens and routers (Plex,
# Jellyfin, Emby) against stand-ins for their backends and MpvController, and
# for the app's VhsNoise (stubs).
add_test(NAME media_players_views COMMAND tree_browser_test -input ${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_media_players.qml
                                          -import ${CMAKE_CURRENT_SOURCE_DIR}/../views
                                          -import ${CMAKE_CURRENT_SOURCE_DIR}/stubs)
set_tests_properties(media_players_views PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")

# Settings → Controls, each controller apart: the labels, the profiles and
# whose a key is (ControllerProfile), and InputManager with pads SDL makes up
# (its virtual joysticks), its settings kept in config.json (AppCore).
find_package(SDL2 CONFIG REQUIRED)
add_executable(controllers_test
    controllers_test.cpp
    ../src/input/InputManager.cpp
    ../src/input/ControllerProfile.cpp
    ../src/input/JammaPins.cpp
    ../src/AppCore.cpp
    ../src/util/LegacyNames.cpp)
target_include_directories(controllers_test PRIVATE ../src)
target_link_libraries(controllers_test PRIVATE
    Qt6::Core Qt6::Gui Qt6::Quick Qt6::Qml Qt6::Network Qt6::Test SDL2::SDL2)
add_test(NAME controllers COMMAND controllers_test)
set_tests_properties(controllers PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# The app itself (the build in build/, made apart from the tests; OSDOS_APP
# for another), started offscreen: the main menu with every module on it, and
# each module as the startup module, open without a QML warning, and the app
# stops on SIGTERM as the service stops it. Skipped without the app, and for a
# module that needs a QML module of Qt's this Qt doesn't have (QtQuick.Effects
# before Qt 6.5).
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
    set(smoke_tests app_main_menu)
    file(GLOB manifests CONFIGURE_DEPENDS ${osdos_root}/modules/*/manifest.json)
    foreach(manifest ${manifests})
        get_filename_component(folder ${manifest} DIRECTORY)
        get_filename_component(folder ${folder} NAME)
        add_test(NAME app_module_${folder} COMMAND bash ${smoke} ${folder})
        list(APPEND smoke_tests app_module_${folder})
    endforeach()
    set_tests_properties(${smoke_tests} PROPERTIES SKIP_RETURN_CODE 77)
endif()
