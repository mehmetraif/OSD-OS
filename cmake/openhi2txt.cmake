# openhi2txt (thirdparty/openhi2txt), which the High Scores module reads MAME's
# high score files with (src/modules/highscores), where zlib and minizip are
# found: libminizip-dev on Debian and Raspberry Pi OS, Homebrew's minizip on
# macOS. minizip's static library is linked in where it is there (both have
# it), so the app needs nothing at run time it didn't need before: zlib is
# Qt's too, and an update of the app runs on a system without minizip. Sets
# OPENHI2TXT_FOUND, and with it the openhi2txt::openhi2txt target: the
# library alone, not its command-line tool nor its tests. Included by the
# app's CMakeLists.txt and the regression tests' (tests/CMakeLists.txt).
find_package(ZLIB QUIET)
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(MINIZIP_PC QUIET minizip)
endif()
# minizip.pc's includedir is the headers' own folder; minizip/unzip.h is
# looked for in the one above it.
find_path(MINIZIP_INCLUDE_DIR NAMES minizip/unzip.h
          HINTS ${MINIZIP_PC_INCLUDEDIR} ${MINIZIP_PC_INCLUDEDIR}/..)
find_library(OSDOS_MINIZIP_STATIC NAMES libminizip.a HINTS ${MINIZIP_PC_LIBDIR})
find_library(OSDOS_MINIZIP_SHARED NAMES minizip HINTS ${MINIZIP_PC_LIBDIR})
set(OPENHI2TXT_FOUND OFF)

# In a scope of its own, so what it sets for openhi2txt stays there.
function(osdos_add_openhi2txt)
    set(OPENHI2TXT_BUILD_CLI OFF)
    set(BUILD_TESTING OFF)
    set(CMAKE_AUTOMOC OFF)
    add_subdirectory("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../thirdparty/openhi2txt"
                     "${CMAKE_BINARY_DIR}/openhi2txt" EXCLUDE_FROM_ALL)
endfunction()

if(ZLIB_FOUND AND MINIZIP_INCLUDE_DIR AND (OSDOS_MINIZIP_STATIC OR OSDOS_MINIZIP_SHARED))
    if(OSDOS_MINIZIP_STATIC)
        # As a target that needs zlib, so zlib comes after it on the link line.
        add_library(osdos_minizip STATIC IMPORTED)
        set_target_properties(osdos_minizip PROPERTIES
            IMPORTED_LOCATION "${OSDOS_MINIZIP_STATIC}"
            INTERFACE_LINK_LIBRARIES ZLIB::ZLIB)
        set(MINIZIP_LIBRARY osdos_minizip CACHE STRING "minizip, as openhi2txt links it" FORCE)
    else()
        set(MINIZIP_LIBRARY "${OSDOS_MINIZIP_SHARED}" CACHE STRING "minizip, as openhi2txt links it" FORCE)
    endif()
    set(OPENHI2TXT_FOUND ON)
    osdos_add_openhi2txt()
endif()
