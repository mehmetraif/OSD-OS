#!/bin/sh
# OSD/OS from a terminal: downloads the latest release's file for this
# computer, or the image for a Raspberry Pi, into the current folder, checks
# it against the SHA-256 GitHub keeps for it, and says what comes next. It
# installs nothing and asks for no password.
#
#   curl -fsSL https://osdos.tv/get.sh | sh                this computer's file
#   curl -fsSL https://osdos.tv/get.sh | sh -s -- image    the Raspberry Pi image
#   fetch -qo - https://osdos.tv/get.sh | sh               on FreeBSD
#   ftp -MVo - https://osdos.tv/get.sh | sh                on OpenBSD
#
# This computer's file: the DMG on an Apple Silicon Mac, the AppImage on
# Linux x86_64, install.sh on Linux arm64 (Raspberry Pi OS, where it installs
# the app), the package on FreeBSD and OpenBSD (amd64), and the zip on
# Windows (Git Bash). Windows' PowerShell has a script of its own:
#   irm https://osdos.tv/get.ps1 | iex
#
# The release is read from GitHub's API. OSDOS_RELEASE_URL names another
# record to read: a release's own (.../releases/tags/<tag>), or a test's.
#
# Everything runs from main, at the very end: a download of this script cut
# short runs nothing.

REPO=mehmetraif/OSD-OS
INSTALL=https://github.com/$REPO/blob/main/INSTALL.md
IMAGE_HINT="The OSD/OS image, for a Raspberry Pi, downloads on any computer: curl -fsSL https://osdos.tv/get.sh | sh -s -- image"

say() { printf '%s\n' "$*"; }
die() { printf 'Error: %s\n' "$*" >&2; exit 1; }

usage() {
    say "get.sh downloads OSD/OS's latest release into the current folder:"
    say "  curl -fsSL https://osdos.tv/get.sh | sh                the file for this computer"
    say "  curl -fsSL https://osdos.tv/get.sh | sh -s -- image    the OSD/OS image, for a Raspberry Pi 4 or 5"
}

# What downloads here: curl or wget, else the system's own, FreeBSD's fetch
# or OpenBSD's ftp (another system's ftp may not speak HTTPS).
pick_downloader() {
    for candidate in curl wget fetch ftp; do
        command -v "$candidate" > /dev/null 2>&1 || continue
        if [ "$candidate" != ftp ] || [ "$os" = OpenBSD ]; then
            tool=$candidate
            return 0
        fi
    done
    die "get.sh downloads with curl, wget, fetch or OpenBSD's ftp, and none of them is here."
}

# $1 to standard output, quietly; or with $2, into that file, with its
# progress on a terminal.
download() {
    if [ $# -eq 1 ]; then
        case $tool in
            curl)  curl -fsSL "$1" ;;
            wget)  wget -qO- "$1" ;;
            fetch) fetch -qo - "$1" ;;
            ftp)   ftp -MVo - "$1" ;;
        esac
    elif [ -t 2 ]; then
        case $tool in
            curl)  curl -fL --progress-bar -o "$2" "$1" ;;
            wget)  case $(wget --help 2>&1) in
                       *--show-progress*) wget -q --show-progress -O "$2" "$1" ;;
                       *)                 wget -O "$2" "$1" ;;
                   esac ;;
            fetch) fetch -o "$2" "$1" ;;
            ftp)   ftp -o "$2" "$1" ;;
        esac
    else
        case $tool in
            curl)  curl -fsSL -o "$2" "$1" ;;
            wget)  wget -q -O "$2" "$1" ;;
            fetch) fetch -qo "$2" "$1" ;;
            ftp)   ftp -MVo "$2" "$1" ;;
        esac
    fi
}

# A file's SHA-256, by whichever tool this system has; nothing without one.
sha256_of() {
    if command -v sha256sum > /dev/null 2>&1; then
        sha256sum "$1" | awk '{ print $1 }'
    elif command -v shasum > /dev/null 2>&1; then
        shasum -a 256 "$1" | awk '{ print $1 }'
    elif command -v sha256 > /dev/null 2>&1; then
        sha256 -q "$1"
    elif command -v openssl > /dev/null 2>&1; then
        openssl dgst -sha256 "$1" | awk '{ print $NF }'
    fi
}

# A size as the release page gives it, in KB, MB or GB of 1024.
size_of() {
    awk -v s="$1" 'BEGIN {
        s += 0
        if (s < 1048576)         printf "%.0f KB", s / 1024
        else if (s < 104857600)  printf "%.1f MB", s / 1048576
        else if (s < 1073741824) printf "%.0f MB", s / 1048576
        else                     printf "%.2f GB", s / 1073741824
    }'
}

# The file whose name ends with $1 in the release record on standard input:
# the release's tag, then the file's name, size, digest and link, a line
# each (the file's four empty when the release has no such file), and "end".
# The record is read as JSON, token by token, wherever its lines break and in
# whatever order its keys come: a release's notes are one string, and nothing
# inside them is taken for the record's own. A record that isn't a release's
# fails.
release_file() {
    LC_ALL=C awk -v want="$1" '
        function scalar(v) {
            if (depth == 1 && key[1] == "tag_name")
                tag = v
            else if (depth == 3 && type[2] == "[" && key[1] == "assets")
                field[key[3]] = v
        }
        # A string as it reads, its \" \\ and \/ one character each: all a
        # name, a digest or a link may hold.
        function unescape(v, out, i) {
            out = ""
            while ((i = index(v, "\\")) > 0) {
                out = out substr(v, 1, i - 1) substr(v, i + 1, 1)
                v = substr(v, i + 2)
            }
            return out v
        }
        function asset_done(name) {
            name = field["name"]
            if (!found && length(name) >= length(want) &&
                substr(name, length(name) - length(want) + 1) == want) {
                found = 1
                fname = name
                fsize = field["size"]
                fdigest = field["digest"]
                furl = field["browser_download_url"]
            }
            split("", field)
        }
        {
            s = $0
            while (s != "") {
                c = substr(s, 1, 1)
                if (c == "\"") {
                    if (!match(s, /^"([^"\\]|\\.)*"/)) {
                        bad = 1
                        exit
                    }
                    v = substr(s, 2, RLENGTH - 2)
                    s = substr(s, RLENGTH + 1)
                    if (type[depth] == "{" && keynext) {
                        key[depth] = v
                        keynext = 0
                    } else
                        scalar(v)
                    continue
                }
                if (c == "{" || c == "[") {
                    type[++depth] = c
                    key[depth] = ""
                    keynext = (c == "{")
                } else if (c == "}" || c == "]") {
                    if (c == "}" && depth == 3 && type[2] == "[" && key[1] == "assets")
                        asset_done()
                    depth--
                    keynext = 0
                } else if (c == ",")
                    keynext = (type[depth] == "{")
                else if (c != ":" && c != " " && c != "\t" && c != "\r") {
                    # A number, true, false or null.
                    if (!match(s, /^[-+.0-9A-Za-z]+/)) {
                        bad = 1
                        exit
                    }
                    scalar(substr(s, 1, RLENGTH))
                    s = substr(s, RLENGTH + 1)
                    continue
                }
                s = substr(s, 2)
            }
        }
        END {
            if (bad || depth != 0 || tag == "")
                exit 2
            if (fdigest == "null")
                fdigest = ""
            print unescape(tag)
            print unescape(fname)
            print fsize
            print unescape(fdigest)
            print unescape(furl)
            print "end"
        }'
}

main() {
    set -eu
    [ $# -le 1 ] || { usage >&2; exit 2; }
    case ${1:-app} in
        app|image) what=${1:-app} ;;
        -h|--help|help) usage; exit 0 ;;
        *) usage >&2; exit 2 ;;
    esac

    os=$(uname -s)
    arch=$(uname -m)
    if [ "$what" = image ]; then
        suffix=-raspberry-pi.img.xz kind="Raspberry Pi image"
    else
        case $os/$arch in
            Darwin/*)
                # A Terminal under Rosetta says x86_64 on Apple Silicon too.
                if [ "$arch" = arm64 ] || [ "$(sysctl -n hw.optional.arm64 2> /dev/null)" = 1 ]; then
                    suffix=-macOS-arm64.dmg kind="app for macOS"
                else
                    die "OSD/OS for macOS is built for Apple Silicon, and this Mac is $arch. $IMAGE_HINT"
                fi ;;
            Linux/x86_64|Linux/amd64)  suffix=-linux-x86_64.AppImage kind="AppImage for Linux x86_64" ;;
            Linux/aarch64|Linux/arm64) suffix=install.sh kind="installer for Raspberry Pi OS" ;;
            FreeBSD/amd64)             suffix=-freebsd-amd64.pkg kind="FreeBSD package" ;;
            OpenBSD/amd64)             suffix=-openbsd-amd64.tgz kind="OpenBSD package" ;;
            MINGW*|MSYS*|CYGWIN*)      suffix=-windows-x64.zip kind="app for Windows" ;;
            *) die "OSD/OS has no app for $os on $arch: it runs on Apple Silicon Macs, Linux x86_64, Raspberry Pi OS (arm64), Windows x64, and FreeBSD and OpenBSD on amd64. $IMAGE_HINT" ;;
        esac
    fi

    [ -w . ] || die "$(pwd) can't be written to: run it in a folder of yours, such as your home."
    pick_downloader

    say "Reading OSD/OS's latest release on GitHub..."
    record=$(download "${OSDOS_RELEASE_URL:-https://api.github.com/repos/$REPO/releases/latest}") \
        || die "GitHub didn't answer with OSD/OS's latest release; try again in a while."
    found=$(printf '%s\n' "$record" | release_file "$suffix") \
        || die "GitHub's answer isn't a release's record."
    { IFS= read -r tag; IFS= read -r name; IFS= read -r size; IFS= read -r digest; IFS= read -r url; } << EOF
$found
EOF
    [ -n "$name" ] || die "OSD/OS $tag has no $kind."
    # Saved in this folder alone, under a plain name.
    case $name in
        .*|-*|*[!A-Za-z0-9._+-]*) die "the release's file has a name get.sh doesn't save: $name" ;;
    esac
    # Checked when GitHub keeps a SHA-256 for it, as it does for every file
    # a release has had since June 2025.
    hex=
    case $digest in
        sha256:*) hex=${digest#sha256:} ;;
    esac
    case $hex in
        *[!0-9a-f]*) hex= ;;
    esac
    [ ${#hex} -eq 64 ] || hex=
    if [ -n "$hex" ] && [ -z "$(sha256_of /dev/null)" ]; then
        die "nothing here computes a SHA-256 (sha256sum, shasum, sha256 or openssl), so $name can't be checked."
    fi

    if [ -n "$hex" ] && [ -f "$name" ] && [ "$(sha256_of "$name")" = "$hex" ]; then
        say "$name is here already, and matches the SHA-256 GitHub keeps for it."
    else
        case $size in
            ''|*[!0-9]*) say "Downloading $name, OSD/OS $tag..." ;;
            *) say "Downloading $name ($(size_of "$size")), OSD/OS $tag..." ;;
        esac
        # Into a file of its own until it is checked: a download cut short or
        # gone wrong never stands under the file's name.
        part=$name.part
        trap 'rm -f "$part"' EXIT
        trap 'exit 129' HUP
        trap 'exit 130' INT
        trap 'exit 143' TERM
        download "$url" "$part" || die "the download of $name didn't finish."
        if [ -n "$hex" ]; then
            [ "$(sha256_of "$part")" = "$hex" ] \
                || die "$name doesn't match the SHA-256 GitHub keeps for it, so it is deleted again."
            say "Checked: it matches the SHA-256 GitHub keeps for it."
        else
            say "GitHub keeps no SHA-256 for $name, so it isn't checked."
        fi
        mv -f "$part" "$name"
        trap - EXIT HUP INT TERM
    fi

    say ""
    case $suffix in
        -raspberry-pi.img.xz)
            say "Next: write it to an SD card with Raspberry Pi Imager (Choose OS, Use Custom),"
            say "and choose what the card starts with on OSD/OS Setup: https://osdos.tv/setup/"
            say "More: https://github.com/$REPO/blob/main/os/README.md#flashing" ;;
        -macOS-arm64.dmg)
            say "Next: open it, and move osdos.app into Applications:"
            say "    open $name"
            say "OSD/OS plays videos with mpv: brew install mpv"
            say "More: $INSTALL#on-macos-arm" ;;
        -linux-x86_64.AppImage)
            chmod +x "$name"
            say "Next: start it:"
            say "    ./$name"
            say "More: $INSTALL#on-steamos--linux-x86_64" ;;
        install.sh)
            say "Next: on Raspberry Pi OS, it installs OSD/OS as an app, with the packages it"
            say "needs (it asks for your password, for sudo):"
            say "    bash install.sh"
            say "$IMAGE_HINT"
            say "More: $INSTALL#on-a-raspberry-pi" ;;
        -freebsd-amd64.pkg)
            say "Next: as root, add the packages it needs, then OSD/OS, and start it with osdos:"
            say "    pkg install -y qt6-base qt6-declarative qt6-svg sdl2 minizip mpv"
            say "    pkg add $name"
            say "More: $INSTALL#on-freebsd-amd64" ;;
        -openbsd-amd64.tgz)
            say "Next: as root, add it under the name it has (the packages it needs come with"
            say "it), and start it with osdos:"
            say "    pkg_add -D unsigned $name"
            say "More: $INSTALL#on-openbsd-amd64" ;;
        -windows-x64.zip)
            say "Next: unzip it where you want to keep OSD/OS, put mpv in its osdos folder"
            say "(https://mpv.io/installation/), and start osdos.exe."
            say "More: $INSTALL#on-windows-x64" ;;
    esac
}

main "$@"
