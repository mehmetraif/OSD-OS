# The engines' versions every OSD/OS machine plays with, read from
# scripts/engines.env (FinalBurn Neo's commit and date, RetroArch's and
# MAME's versions) and given to a target as compile definitions:
# OSDOS_FBNEO_COMMIT, OSDOS_FBNEO_DATE, OSDOS_RETROARCH_VERSION and
# OSDOS_MAME_VERSION, each a string. A change to the file configures the
# build again.
set(OSDOS_ENGINES_FILE "${CMAKE_CURRENT_LIST_DIR}/../scripts/engines.env")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${OSDOS_ENGINES_FILE}")
file(STRINGS "${OSDOS_ENGINES_FILE}" _osdos_engine_lines REGEX "^[A-Z_]+=")
foreach(_line IN LISTS _osdos_engine_lines)
    string(REGEX REPLACE "^([A-Z_]+)=(.*)$" "\\1" _key "${_line}")
    string(REGEX REPLACE "^([A-Z_]+)=(.*)$" "\\2" _value "${_line}")
    set(OSDOS_ENGINE_${_key} "${_value}")
endforeach()
foreach(_key FBNEO_COMMIT FBNEO_DATE RETROARCH_VERSION MAME_VERSION)
    if(NOT OSDOS_ENGINE_${_key})
        message(FATAL_ERROR "scripts/engines.env names no ${_key}")
    endif()
endforeach()

function(osdos_engine_definitions target)
    target_compile_definitions(${target} PRIVATE
        OSDOS_FBNEO_COMMIT="${OSDOS_ENGINE_FBNEO_COMMIT}"
        OSDOS_FBNEO_DATE="${OSDOS_ENGINE_FBNEO_DATE}"
        OSDOS_RETROARCH_VERSION="${OSDOS_ENGINE_RETROARCH_VERSION}"
        OSDOS_MAME_VERSION="${OSDOS_ENGINE_MAME_VERSION}")
endfunction()
