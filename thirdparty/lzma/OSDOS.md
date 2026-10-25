# The LZMA SDK in OSD/OS

The [LZMA SDK](https://7-zip.org/sdk.html)'s 7z decoder, in C, which `src/util/SevenZip.cpp` unpacks 7z archives with: the builds of RetroArch and MAME for Windows, which Settings → Arcade → FinalBurn Neo and → MAME download (`src/modules/arcade/ProgramInstaller.cpp`), MAME's a self-extracting archive, a program with the archive appended, read from where the archive begins.

- **Version**: 25.01 (`lzma2501.7z`), its `C/` files as they are, byte for byte.
- **Licence**: public domain, as `lzma-sdk.txt` (the SDK's own note, copied here from its `DOC/`) and the head of every file say. Settings → About shows the note (`assets/licenses/LZMA-SDK.txt`).
- **Taken**: what the SDK's own `7zdec` (`C/Util/7z`) is built of, less its `7zMain.c` and its file I/O (`7zFile.c`; the archive is read through Qt's `QFile`, which lets it begin where it does in a self-extracting one): the 7z reader (`7zArcIn.c`, `7zDec.c`, `7zStream.c`, `7zBuf.c`, `7zBuf2.c`, `7zAlloc.c`, `7zCrc.c`, `7zCrcOpt.c`), the LZMA, LZMA2 and PPMd decoders (`LzmaDec.c`, `Lzma2Dec.c`, `Ppmd7.c`, `Ppmd7Dec.c`), the filters 7-Zip puts before them (`Bcj2.c`, `Bra.c`, `Bra86.c`, `BraIA64.c`, `Delta.c`), `CpuArch.c`, and the headers they include. Left out: the encoders, the other formats (xz, zstd, …), the assembly, and every other language's sources.
- **Built** by `cmake/lzma.cmake` as the static library `osdos_lzma`, with `Z7_PPMD_SUPPORT` and `Z7_EXTRACT_ONLY` as `7zdec`'s makefile sets them, on every system: the extractor and its tests (`tests/sevenzip_test.cpp`) are the same everywhere.

To update it, take a newer SDK's `C/` files of the same names (and any new header they include) and its `DOC/lzma-sdk.txt` over these, and note the version above. Nothing in them is changed.
