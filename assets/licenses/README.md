# Licences shown in About

Settings → System → About shows the licence of each piece of free software
OSD/OS uses. The GNU GPL v3 is OSD/OS's own `LICENSE`; these are the others,
which ship with every build:

- `GPL-2.0.txt`: the GNU General Public License, version 2, for MAME (version
  2 or later), hi2txt's game definitions and the 240p Test Suite's pictures
  in Settings → Screen → Test Patterns (version 2 or later, `assets/testpatterns`).
- `RapidXML.txt`: RapidXML's, the Boost Software License 1.0 or the MIT
  License, as in `thirdparty/openhi2txt/thirdparty/rapidxml-1.13/LICENSE`.
- `zlib.txt`: minizip's and zlib's notices, under the zlib License.
- `LZMA-SDK.txt`: the LZMA SDK's note, public domain, for its 7z decoder
  (`thirdparty/lzma`), which unpacks the MAME and RetroArch for Windows
  that the MAME and FinalBurn Neo pages download.
- `qrcodegen.txt`: Project Nayuki's QR Code generator library's notice,
  under the MIT License (`thirdparty/qrcodegen`), which draws the codes of
  Settings → System → Export to OSD/OS Setup.
- `LGPL-3.0.txt`: the GNU Lesser General Public License, version 3, for Qt,
  which the Windows folder, the Mac's app and the AppImage carry. Qt Quick
  3D (the Arcade's Orb look) is under the GPL v3. Qt's source:
  https://download.qt.io/official_releases/qt/.
- `Apache-2.0.txt`: the Apache License, version 2.0, for OpenSSL
  (https://www.openssl.org/source/), which those packages carry too.
- `Icons.txt`: the notices of the icons others drew: Weather Icons (SIL Open
  Font License 1.1), Phosphor Icons (MIT), Simple Icons (CC0) and Jellyfin's
  logo (CC BY-SA 4.0).
- `Hash-without-Sine.txt`: Dave Hoskins' notice, under the MIT License, for
  the hash in the shaders' `grain()`.

The AppImage also carries mpv (GPL v2 or later, `GPL-2.0.txt`), built by
`scripts/build-mpv.sh` from https://github.com/mpv-player/mpv at the tag the
script names, with Ubuntu's FFmpeg and the libraries they need, whose
source is in Ubuntu's archive.

The Linux arm64 tarball, and so the OSD/OS image, also carries RetroArch
(GPL v3, OSD/OS's own `LICENSE`), built by `scripts/build-retroarch.sh` from
https://github.com/libretro/RetroArch at the commit `scripts/engines.env`
names, unchanged, with TLS. The licences of the libraries built into it from
its source (BearSSL, FLAC, rcheevos, SPIRV-Cross and others) are beside it,
in `share/osdos/retroarch/licenses.txt`.

FinalBurn Neo's licence comes with the core: on the OSD/OS image, or
downloaded with it from Settings → Arcade → FinalBurn Neo.
