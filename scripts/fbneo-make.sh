# shellcheck shell=bash disable=SC2034  # ext and strip are for the script that sources this
# FinalBurn Neo's libretro core made on each system, as scripts/build-fbneo.sh
# (the whole core) and scripts/build-fbneo-lan.sh (the LAN core) make it:
# both source this file and call
#
#   fbneo_make <FBNeo source folder> GIT_VERSION=<version> GIT_DATE=<date>
#
# which builds src/burner/libretro/fbneo_libretro.<ext> in the source with GNU
# make, a job per processor, and the system's settings below, and sets ext,
# the core's file extension on this system, and strip, the command that
# strips the core.
#
# On every system the compilers are told not to fuse a multiplication and
# an addition into one instruction (-ffp-contract=off): GCC does by default
# on aarch64, where the result is rounded once instead of twice, and not on
# x86-64 without FMA, so a game whose state holds a float (a sound filter,
# some drivers) would compute it differently on a Raspberry Pi than on a PC,
# and a network game between them would fall out of step. Clang, macOS's and
# the BSDs', fuses within a statement by default. It goes in from the
# environment, in FBNEO_DEFINES, which the makefile adds its own to
# (FBNEO_DEFINES +=) and hands to every compile; one on make's command line
# would replace them. No CPU flags (-mcpu, -march): every machine of a
# release plays the same cores.
#
# On FreeBSD and OpenBSD: the makefile's Unix build with the system's
# compiler (cc, c++), without -no-undefined, which OpenBSD can't link a
# library with (its libraries leave libc's symbols to the program that loads
# them), and with GBAPPU_WORKER_DISABLE, a switch of FinalBurn Neo's own
# (src/burn/drv/gba/gbappu_worker.h) that draws the Game Boy Advance's
# picture on the emulator's thread instead of a thread of its own: that
# thread is named with macOS's pthread_setname_np(name) on every BSD, which
# FreeBSD's takes the thread as well and OpenBSD hasn't, so it doesn't compile
# on either. The arcade games, all OSD/OS plays, don't use it.
set -euo pipefail

fbneo_make() {
    local src=$1 make jobs args
    shift
    make=$(command -v gmake || command -v make)
    # glibc's getconf knows the name with its underscore alone, the BSDs'
    # without it.
    jobs=$(getconf _NPROCESSORS_ONLN 2>/dev/null || getconf NPROCESSORS_ONLN 2>/dev/null \
           || sysctl -n hw.ncpu 2>/dev/null || echo 2)
    args=("$@")
    strip=(strip)
    export FBNEO_DEFINES="${FBNEO_DEFINES:+$FBNEO_DEFINES }-ffp-contract=off"
    case "$(uname -s)" in
        Darwin)
            ext=dylib
            # The oldest macOS the app runs on, not the one it is built on.
            export MACOSX_DEPLOYMENT_TARGET=${MACOSX_DEPLOYMENT_TARGET:-11.0}
            # A library's own symbols are what RetroArch loads it by.
            strip=(strip -x) ;;
        MINGW* | MSYS*)
            ext=dll
            # MinGW's compilers by name: make's own default for C is cc.
            args+=(CC=gcc CXX=g++)
            # From the environment, which the makefile's LDFLAGS += adds its own
            # to: winpthreads, which MinGW's libstdc++ is built on, in the DLL too.
            export LDFLAGS=-static ;;
        *BSD)
            ext=so
            # $(VERSION_SCRIPT) is the makefile's own, for make to expand. The
            # define goes in from the environment, as the flag above.
            # shellcheck disable=SC2016
            args+=(CC=cc CXX=c++ SHARED='-shared -Wl,--version-script=$(VERSION_SCRIPT)')
            FBNEO_DEFINES+=" -DGBAPPU_WORKER_DISABLE" ;;
        *)
            ext=so ;;
    esac
    "$make" -C "$src/src/burner/libretro" -j"$jobs" "${args[@]}"
}
