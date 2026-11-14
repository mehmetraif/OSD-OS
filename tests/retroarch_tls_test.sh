#!/usr/bin/env bash
# The RetroArch OSD/OS builds for its Linux arm64 package
# (scripts/build-retroarch.sh), started on a game against a stand-in for
# RetroAchievements' site (retroarch_tls/server.py): that it asks over https
# on its own, signs in and earns an achievement when the certificate is good,
# and sends nothing, its password least of all, when the certificate is
# signed by no one the system trusts, is for another name or has run out.
#
#   sudo bash tests/retroarch_tls_test.sh <retroarch>
#
# The certificates are made here: chains like the ones web servers send (an
# EC one, an RSA one, and an EC one under an RSA root), and the three bad
# ones. RetroArch runs in a mount namespace of its own, where the system's
# certificates file has the test's roots added and /etc/hosts names this
# machine as retroachievements.org; nothing of the system's is changed, and
# nothing leaves the machine. Its core is retroarch_tls/core.c, which runs a
# black picture and has one byte for an achievement to watch.
#
# It needs root (the namespace, and port 443), python3, openssl, a C
# compiler and unshare. Without them it is skipped (exit 77).
set -uo pipefail

program=${1:?usage: retroarch_tls_test.sh <retroarch>}
here=$(cd "$(dirname "$0")" && pwd)
system_certificates=/etc/ssl/certs/ca-certificates.crt

skip() { echo "skipped: $*"; exit 77; }
[ "$(id -u)" = 0 ] || skip "it needs root"
for tool in python3 openssl unshare mount; do
    command -v "$tool" > /dev/null || skip "no $tool"
done
compiler=$(command -v "${CC:-cc}" || command -v gcc || true)
[ -n "$compiler" ] || skip "no C compiler"
[ -f "$system_certificates" ] || skip "no $system_certificates"
[ -x "$program" ] || { echo "no program at $program" >&2; exit 1; }
program=$(cd "$(dirname "$program")" && pwd)/$(basename "$program")

work=$(mktemp -d)
server=""
player=""
cleanup() {
    [ -z "$player" ] || kill "$player" 2> /dev/null
    [ -z "$server" ] || kill "$server" 2> /dev/null
    wait 2> /dev/null
    rm -rf "$work"
}
trap cleanup EXIT

# ── The certificates ──────────────────────────────────────────────────────────
pki=$work/pki
mkdir "$pki"
quiet() { "$@" > "$pki/openssl.log" 2>&1 || { echo "failed: $*" >&2; cat "$pki/openssl.log" >&2; exit 1; }; }
extensions() { printf '%s\n' "$@" > "$pki/extensions"; }
key() { # <file> ec256|ec384|rsa2048|rsa4096
    case "$2" in
        ec256) quiet openssl ecparam -name prime256v1 -genkey -noout -out "$1" ;;
        ec384) quiet openssl ecparam -name secp384r1 -genkey -noout -out "$1" ;;
        rsa*) quiet openssl genrsa -out "$1" "${2#rsa}" ;;
    esac
}
root() { # <name> <key kind> <digest> <common name>
    key "$pki/$1.key" "$2"
    quiet openssl req -x509 -new -key "$pki/$1.key" "-$3" -days 3650 -subj "/O=OSD-OS Test/CN=$4" -out "$pki/$1.pem" \
        -addext "basicConstraints=critical,CA:TRUE" -addext "keyUsage=critical,keyCertSign,cRLSign"
}
intermediate() { # <name> <key kind> <issuer> <digest> <common name>
    key "$pki/$1.key" "$2"
    quiet openssl req -new -key "$pki/$1.key" -subj "/O=OSD-OS Test/CN=$5" -out "$pki/$1.csr"
    extensions "basicConstraints=critical,CA:TRUE,pathlen:0" "keyUsage=critical,digitalSignature,keyCertSign,cRLSign"
    quiet openssl x509 -req -in "$pki/$1.csr" -CA "$pki/$3.pem" -CAkey "$pki/$3.key" -CAcreateserial "-$4" -days 1800 \
        -extfile "$pki/extensions" -out "$pki/$1.pem"
}
leaf() { # <name> <key kind> <issuer> <host> [expired]
    key "$pki/$1.key" "$2"
    quiet openssl req -new -key "$pki/$1.key" -subj "/CN=$4" -out "$pki/$1.csr"
    extensions "basicConstraints=critical,CA:FALSE" "keyUsage=critical,digitalSignature" "extendedKeyUsage=serverAuth" \
        "subjectAltName=DNS:$4,DNS:*.$4"
    if [ "${5:-}" = expired ]; then
        # openssl ca, which takes dates (x509 -req only does from OpenSSL 3.4).
        printf '%s\n' '[ca]' 'default_ca=own' '[own]' "dir=$pki" "database=$pki/index" "new_certs_dir=$pki" \
            "serial=$pki/serial" 'policy=any' 'default_md=sha256' 'unique_subject=no' '[any]' 'commonName=supplied' > "$pki/ca.cnf"
        : > "$pki/index"
        echo 1000 > "$pki/serial"
        quiet openssl ca -batch -config "$pki/ca.cnf" -in "$pki/$1.csr" -cert "$pki/$3.pem" -keyfile "$pki/$3.key" \
            -startdate 250101000000Z -enddate 250401000000Z -extfile "$pki/extensions" -notext -out "$pki/$1.pem"
    else
        quiet openssl x509 -req -in "$pki/$1.csr" -CA "$pki/$3.pem" -CAkey "$pki/$3.key" -CAcreateserial -sha256 -days 90 \
            -extfile "$pki/extensions" -out "$pki/$1.pem"
    fi
    cat "$pki/$1.pem" "$pki/$3.pem" > "$pki/$1.chain.pem"
}
# A P-384 root over a P-256 intermediate, as a CDN's; an RSA chain; and an
# EC intermediate under an RSA root.
root root-ec ec384 sha384 "Test Root EC"
intermediate inter-ec ec256 root-ec sha384 "Test Intermediate EC"
root root-rsa rsa4096 sha256 "Test Root RSA"
intermediate inter-rsa rsa2048 root-rsa sha256 "Test Intermediate RSA"
intermediate inter-mixed ec384 root-rsa sha256 "Test Intermediate EC under RSA"
# A root the system never heard of.
root stranger ec256 sha256 "Someone Else's Root"
leaf good-ec ec256 inter-ec retroachievements.org
leaf good-rsa rsa2048 inter-rsa retroachievements.org
leaf good-mixed ec256 inter-mixed retroachievements.org
leaf untrusted ec256 stranger retroachievements.org
leaf wrong-name ec256 inter-ec example.org
leaf expired ec256 inter-ec retroachievements.org expired

cat "$system_certificates" "$pki/root-ec.pem" "$pki/root-rsa.pem" > "$work/certificates.crt"
{ cat /etc/hosts; echo "127.0.0.1 retroachievements.org media.retroachievements.org"; } > "$work/hosts"

# ── The core and its game ─────────────────────────────────────────────────────
mkdir "$work/cores"
"$compiler" -shared -fPIC -O1 -o "$work/cores/tlstest_libretro.so" "$here/retroarch_tls/core.c" \
    || { echo "the test core did not build" >&2; exit 1; }
printf 'OSD/OS TLS test\n' > "$work/game.bin"

# ── One game's start ──────────────────────────────────────────────────────────
# start <case> [server option]: the server with the case's certificate, and
# RetroArch on the game, until its log says how the sign-in went (and, signed
# in, that the achievement was earned), at most half a minute.
start() {
    local name=$1 run=$work/$1 waited
    shift
    mkdir "$run"
    python3 "$here/retroarch_tls/server.py" --cert "$pki/$name.chain.pem" --key "$pki/$name.key" \
        --log "$run/server.log" "$@" &
    server=$!
    for waited in $(seq 50); do
        (exec 3<> /dev/tcp/127.0.0.1/443) 2> /dev/null && break
        kill -0 "$server" 2> /dev/null || { echo "the stand-in server did not start (port 443 taken?)" >&2; exit 1; }
        sleep 0.1
    done
    # As the app writes one for a sign-in (libretro::signInConfigText()),
    # with no host of its own: the program's.
    cat > "$run/retroarch.cfg" << CONFIG
config_save_on_exit = "true"
load_dummy_on_core_shutdown = "false"
video_driver = "null"
audio_driver = "null"
audio_enable = "false"
input_driver = "null"
input_joypad_driver = "null"
menu_driver = "rgui"
pause_nonactive = "false"
ui_companion_enable = "false"
system_directory = "$run"
savefile_directory = "$run"
savestate_directory = "$run"
libretro_directory = "$work/cores"
cheevos_enable = "true"
cheevos_username = "exampleplayer"
cheevos_password = "Key456example"
cheevos_token = ""
cheevos_hardcore_mode_enable = "false"
CONFIG
    # shellcheck disable=SC2016
    HOME=$run XDG_CONFIG_HOME=$run/config unshare -m sh -c '
        mount --bind "$1/certificates.crt" "$2" && mount --bind "$1/hosts" /etc/hosts &&
        exec "$3" -L "$1/cores/tlstest_libretro.so" --config "$4/retroarch.cfg" --verbose \
            --log-file="$4/retroarch.log" --max-frames=1800 "$1/game.bin"
    ' sh "$work" "$system_certificates" "$program" "$run" > "$run/output.log" 2>&1 &
    player=$!
    for waited in $(seq 300); do
        if grep -a -q "login failed" "$run/retroarch.log" 2> /dev/null; then
            break
        elif grep -a -q "logged in successfully" "$run/retroarch.log" 2> /dev/null \
            && grep -a -q "Achievement 1 awarded" "$run/retroarch.log"; then
            break
        fi
        kill -0 "$player" 2> /dev/null || break
        sleep 0.1
    done
    # Ended as a game is, so that it saves its config, the token in it.
    kill -TERM "$player" 2> /dev/null
    wait "$player" 2> /dev/null
    player=""
    kill "$server" 2> /dev/null
    wait "$server" 2> /dev/null
    server=""
}

failures=0
failed() {
    echo "FAIL $1: $2"
    sed 's/^/    server: /' "$work/$1/server.log" 2> /dev/null | cut -c1-160 | head -8
    grep -a "RCHEEVOS\]" "$work/$1/retroarch.log" 2> /dev/null | grep -a -i "host\|log\|fail\|award" | sed 's/^/    retroarch: /' | cut -c1-160 | head -8
    tail -n 2 "$work/$1/output.log" 2> /dev/null | sed 's/^/    output: /' | cut -c1-160
    failures=$((failures + 1))
}

# A good certificate: signed in over TLS, the achievement earned, the token
# kept, and every request of the site's over TLS 1.2 or later by its name.
accepted() {
    local name=$1 run=$work/$1 over
    start "$@"
    grep -a -q "Using host: https://retroachievements.org" "$run/retroarch.log" 2> /dev/null \
        || { failed "$name" "RetroArch did not ask over https"; return; }
    grep -a -q "exampleplayer logged in successfully" "$run/retroarch.log" \
        || { failed "$name" "it did not sign in"; return; }
    grep -a -q "Achievement 1 awarded" "$run/retroarch.log" \
        || { failed "$name" "the achievement was not earned"; return; }
    grep -q 'API .*r=login2.* host=retroachievements.org' "$run/server.log" \
        || { failed "$name" "the server saw no sign-in"; return; }
    grep -q 'API .*r=awardachievement' "$run/server.log" \
        || { failed "$name" "the server saw no achievement"; return; }
    if grep -v '^REFUSED' "$run/server.log" | grep -q -v '^TLSv1\.[23] '; then
        failed "$name" "a request came over something else than TLS 1.2 or 1.3"
        return
    fi
    grep -q '^cheevos_token = "Tok123exampleABC"' "$run/retroarch.cfg" \
        || { failed "$name" "the token was not kept"; return; }
    over=$(grep -m1 -o '^TLSv[^ ]* [^ ]*' "$run/server.log")
    echo "ok   $name: signed in, an achievement earned, $(grep -c '^TLS' "$run/server.log") requests over $over"
}

# A bad one: nothing asked of the site (the password stays here), and
# RetroArch says the sign-in failed, as with a server that doesn't answer.
refused() {
    local name=$1 run=$work/$1
    start "$@"
    if grep -q '^TLS' "$run/server.log" 2> /dev/null; then
        failed "$name" "RetroArch took the certificate and asked the site"
        return
    fi
    grep -a -q "login failed" "$run/retroarch.log" 2> /dev/null \
        || { failed "$name" "RetroArch did not say the sign-in failed"; return; }
    if grep -q '^cheevos_token = "Tok' "$run/retroarch.cfg"; then
        failed "$name" "a token was kept"
        return
    fi
    echo "ok   $name: refused, nothing sent"
}

accepted good-ec
accepted good-rsa --chunked
accepted good-mixed
refused untrusted
refused wrong-name
refused expired

if [ "$failures" -gt 0 ]; then
    echo "$failures of 6 failed"
    exit 1
fi
echo "all 6 passed"
