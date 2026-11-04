# Project Nayuki's QR Code generator (thirdparty/qrcodegen), which draws the
# codes of Settings → System → Export to OSD/OS Setup (src/util/SetupExport).
# MIT License, built into the app as the static library osdos_qrcodegen on
# every system. Included by the app's CMakeLists.txt and the regression
# tests' (tests/CMakeLists.txt).
set(OSDOS_QRCODEGEN_DIR "${CMAKE_CURRENT_LIST_DIR}/../thirdparty/qrcodegen")
add_library(osdos_qrcodegen STATIC "${OSDOS_QRCODEGEN_DIR}/qrcodegen.cpp")
target_include_directories(osdos_qrcodegen PUBLIC "${OSDOS_QRCODEGEN_DIR}")
set_target_properties(osdos_qrcodegen PROPERTIES POSITION_INDEPENDENT_CODE ON)
