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
        PRESETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../os/stage-osdos/03-boot/files")
    target_link_libraries(display_output_test PRIVATE Qt6::Core Qt6::Network Qt6::Test)
    add_test(NAME display_output COMMAND display_output_test)
    # The same as a Pi 5, whose outputs are others.
    add_test(NAME display_output_pi5 COMMAND display_output_test)
    set_tests_properties(display_output_pi5 PROPERTIES
        ENVIRONMENT "OSDOS_BOARD_MODEL=Raspberry Pi 5 Model B Rev 1.0")

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
endif()

find_package(Qt6 REQUIRED COMPONENTS Quick QuickTest)
add_executable(tree_browser_test tree_browser_test.cpp)
target_link_libraries(tree_browser_test PRIVATE Qt6::QuickTest Qt6::Quick)
add_test(NAME tree_browser COMMAND tree_browser_test -input ${CMAKE_CURRENT_SOURCE_DIR}/qml)
set_tests_properties(tree_browser PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
