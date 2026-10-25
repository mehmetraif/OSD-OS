# The LZMA SDK's 7z decoder (thirdparty/lzma), which the Arcade module
# unpacks RetroArch's build for Windows with (src/util/SevenZip.cpp, for
# RetroArchInstaller): LZMA, LZMA2 and PPMd, and the BCJ, BCJ2, ARM, ARM64,
# Delta and other filters 7-Zip puts before them, decoding only
# (Z7_EXTRACT_ONLY), as the SDK's own 7zdec is built. Public domain, built
# into the app as the static library osdos_lzma on every system, so that the
# extractor and its tests are the same everywhere. Included by the app's
# CMakeLists.txt and the regression tests' (tests/CMakeLists.txt).
enable_language(C)
set(OSDOS_LZMA_DIR "${CMAKE_CURRENT_LIST_DIR}/../thirdparty/lzma")
add_library(osdos_lzma STATIC
    "${OSDOS_LZMA_DIR}/7zAlloc.c"
    "${OSDOS_LZMA_DIR}/7zArcIn.c"
    "${OSDOS_LZMA_DIR}/7zBuf.c"
    "${OSDOS_LZMA_DIR}/7zBuf2.c"
    "${OSDOS_LZMA_DIR}/7zCrc.c"
    "${OSDOS_LZMA_DIR}/7zCrcOpt.c"
    "${OSDOS_LZMA_DIR}/7zDec.c"
    "${OSDOS_LZMA_DIR}/7zFile.c"
    "${OSDOS_LZMA_DIR}/7zStream.c"
    "${OSDOS_LZMA_DIR}/Bcj2.c"
    "${OSDOS_LZMA_DIR}/Bra.c"
    "${OSDOS_LZMA_DIR}/Bra86.c"
    "${OSDOS_LZMA_DIR}/BraIA64.c"
    "${OSDOS_LZMA_DIR}/CpuArch.c"
    "${OSDOS_LZMA_DIR}/Delta.c"
    "${OSDOS_LZMA_DIR}/Lzma2Dec.c"
    "${OSDOS_LZMA_DIR}/LzmaDec.c"
    "${OSDOS_LZMA_DIR}/Ppmd7.c"
    "${OSDOS_LZMA_DIR}/Ppmd7Dec.c")
target_include_directories(osdos_lzma PUBLIC "${OSDOS_LZMA_DIR}")
target_compile_definitions(osdos_lzma PRIVATE Z7_PPMD_SUPPORT Z7_EXTRACT_ONLY)
set_target_properties(osdos_lzma PROPERTIES AUTOMOC OFF POSITION_INDEPENDENT_CODE ON)
