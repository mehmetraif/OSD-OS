# QR Code generator in OSD/OS

[Project Nayuki's QR Code generator library](https://www.nayuki.io/page/qr-code-generator-library), in C++, which `src/util/SetupExport.cpp` draws the codes of Settings → System → Export to OSD/OS Setup with: a phone opens OSD/OS Setup's page from them, its form filled in with the device's settings.

- **Version**: the repository's `master` at commit `bcfe1b018cec5f1f91b5705eca5bb4cdd34e9f8d` (github.com/nayuki/QR-Code-generator), its `cpp/qrcodegen.cpp` and `cpp/qrcodegen.hpp` as they are, byte for byte.
- **Licence**: the MIT License, as the head of both files says. Settings → System → About shows it (`assets/licenses/qrcodegen.txt`).
- **Built** by `cmake/qrcodegen.cmake` as the static library `osdos_qrcodegen`, on every system, for the app and its tests (`tests/setup_export_test.cpp`).

To update it, take a newer commit's `cpp/qrcodegen.cpp` and `cpp/qrcodegen.hpp` over these, and note the commit above. Nothing in them is changed.
