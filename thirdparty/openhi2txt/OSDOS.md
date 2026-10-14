# openhi2txt in OSD/OS

[openhi2txt](https://github.com/inigomonyota/openhi2txt) reads MAME's high score files with hi2txt's game definitions. The High Scores module (`src/modules/highscores`) uses it as a library.

- **Version**: 0.2.0, commit `a40c1fd`.
- **Licence**: GPL-3.0, in `LICENSE.txt`, as OSD/OS's own. Its parts from others (RapidXML, zlib and minizip's headers) are listed in `THIRD_PARTY_NOTICES.md`.
- **Left out**: the prebuilt static zlib and minizip libraries for Windows builds (`thirdparty/zlib-static/lib`), and the repository's `.gitignore` and `.gitattributes`. Nothing else is changed.

OSD/OS builds the library alone (`OPENHI2TXT_BUILD_CLI` and `BUILD_TESTING` off), linked with the system's zlib and minizip (`libminizip-dev` on Debian and Raspberry Pi OS, Homebrew's `minizip` on a Mac), minizip's static library where it is there. Without minizip the app is built without High Scores' tables (`cmake/openhi2txt.cmake`).

To update it, copy a newer checkout over this folder, leaving out the same files, and note its commit above.
