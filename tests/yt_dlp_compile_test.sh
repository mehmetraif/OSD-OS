#!/usr/bin/env bash
# yt-dlp's zip rebuilt with its bytecode inside (os/stage-osdos/06-youtube:
# yt-dlp-compile), on a stand-in zipapp made here: it runs and prints the
# same, holds a .pyc beside every .py, keeps its first line and mode, imports
# the .pyc without a look at the source (and the source when a .pyc isn't
# this Python's), is left alone once compiled and survives being rebuilt
# twice; and a rebuild that fails half-way (a damaged zip, a source that
# doesn't compile, a new zip that doesn't run, a folder it can't write) or is
# stopped by a game leaves the file as it was. Then the stage and the update
# unit running it. Needs Python 3; no network.
#   yt_dlp_compile_test.sh <os/stage-osdos/06-youtube>
set -u

STAGE="$1"
COMPILE="$STAGE/files/yt-dlp-compile"
UNIT="$STAGE/files/osdos-yt-dlp-update.service"
fails=0
fail() { echo "FAIL: $1"; fails=$((fails + 1)); }

PY=$(command -v python3) || { echo "SKIP: no python3"; exit 0; }
WORK=$(mktemp -d)
trap 'chmod -R u+w "$WORK" 2> /dev/null; rm -rf "$WORK"' EXIT

compile() { "$PY" -I "$COMPILE" "$@"; }
sum() { sha256sum "$1" | cut -d' ' -f1; }
mode() { stat -c %a "$1"; }
leftovers() { find "$1" -maxdepth 1 -name '.yt-dlp-compile-*' | wc -l; }
# Rewrites a zip entry (python3 -I <zip> <name> <text|corrupt-magic>), the
# rest as it was.
rewrite() {
    "$PY" -I - "$@" << 'PY'
import shutil, sys, zipfile
path, name, what = sys.argv[1:4]
with open(path, "rb") as f:
    head = f.readline() if f.read(2) == b"#!" else b""
with zipfile.ZipFile(path) as zf:
    entries = [(i, zf.read(i)) for i in zf.infolist()]
with open(path + ".tmp", "wb") as f:
    f.write(b"#!" + head if head else b"")
    with zipfile.ZipFile(f, "w") as out:
        for info, data in entries:
            if info.filename == name:
                data = b"\0\0\0\0" + data[4:] if what == "corrupt-magic" else what.encode()
            out.writestr(info, data)
shutil.copymode(path, path + ".tmp")
shutil.move(path + ".tmp", path)
PY
}

# The stand-in: a package and a __main__.py, as yt-dlp's zip has them.
mkdir -p "$WORK/src/standin"
cat > "$WORK/src/__main__.py" << 'PY'
import standin.main
standin.main.run()
PY
: > "$WORK/src/standin/__init__.py"
cat > "$WORK/src/standin/main.py" << 'PY'
import sys
def run():
    if sys.argv[1:] == ["--version"]:
        print("2026.11.25")
    else:
        print("stand-in ran with", sys.argv[1:])
PY
mkdir -p "$WORK/bin"
APP="$WORK/bin/yt-dlp"
"$PY" -I -m zipapp "$WORK/src" -p '/usr/bin/env python3' -o "$APP" || { echo "FAIL: zipapp"; exit 1; }
chmod 751 "$APP"
cp -p "$APP" "$WORK/plain"
first=$(head -n 1 "$APP")
before=$("$PY" -I "$APP" a b)
version=$("$PY" -I "$APP" --version)

# Rebuilt: the same output, first line, mode; a .pyc of this Python beside
# every .py, hash-based without a look at the source.
out=$(compile "$APP" 2>&1) || fail "the rebuild failed: $out"
[ "$("$PY" -I "$APP" a b)" = "$before" ] || fail "the rebuilt zip prints otherwise"
[ "$("$PY" -I "$APP" --version)" = "$version" ] || fail "the rebuilt zip's --version differs"
[ "$(head -n 1 "$APP")" = "$first" ] || fail "the first line is '$(head -n 1 "$APP")', not '$first'"
[ "$(mode "$APP")" = 751 ] || fail "the mode is $(mode "$APP"), not 751"
check_pyc() {
    "$PY" -I - "$1" << 'PY'
import importlib.util, sys, zipfile
with zipfile.ZipFile(sys.argv[1]) as zf:
    names = zf.namelist()
    if len(names) != len(set(names)):
        sys.exit("an entry is in the zip twice")
    sources = [n for n in names if n.endswith(".py")]
    if len(sources) != 3:
        sys.exit(f"the zip has {len(sources)} sources, not 3")
    for n in sources:
        if n + "c" not in names:
            sys.exit(f"no {n}c")
        head = zf.read(n + "c")[:8]
        if head != importlib.util.MAGIC_NUMBER + (1).to_bytes(4, "little"):
            sys.exit(f"{n}c is not this Python's unchecked-hash bytecode")
PY
}
out=$(check_pyc "$APP" 2>&1) || fail "$out"
[ "$(leftovers "$WORK/bin")" = 0 ] || fail "the rebuild left its folder behind"

# The .pyc is imported as it is: a source that would fail beside it changes
# nothing. And a .pyc that isn't this Python's (another's magic number) gives
# way to the source beside it.
cp -p "$APP" "$WORK/broken-source"
rewrite "$WORK/broken-source" standin/main.py 'raise SystemExit("the source was compiled")'
out=$("$PY" -I "$WORK/broken-source" a b 2>&1)
[ "$out" = "$before" ] || fail "a .py beside its .pyc was read: $out"
cp -p "$APP" "$WORK/other-python"
rewrite "$WORK/other-python" standin/main.pyc corrupt-magic
out=$("$PY" -I "$WORK/other-python" a b 2>&1)
[ "$out" = "$before" ] || fail "a .pyc of another Python didn't give way to its source: $out"

# Compiled already: left alone. Forced, twice: the same again.
was=$(sum "$APP")
out=$(compile "$APP" 2>&1) || fail "a second run failed: $out"
case "$out" in *"compiled already"*) ;; *) fail "a second run didn't find it compiled: $out" ;; esac
[ "$(sum "$APP")" = "$was" ] || fail "a second run changed the file"
for n in 1 2; do
    out=$(compile --force "$APP" 2>&1) || fail "forced rebuild $n failed: $out"
    [ "$("$PY" -I "$APP" a b)" = "$before" ] || fail "after forced rebuild $n it prints otherwise"
    out=$(check_pyc "$APP" 2>&1) || fail "after forced rebuild $n: $out"
    [ "$(mode "$APP")" = 751 ] || fail "after forced rebuild $n the mode is $(mode "$APP")"
done
# Run straight, by its first line, as mpv's hook and the app run it.
[ "$(PATH="$(dirname "$PY"):$PATH" "$APP" a b)" = "$before" ] || fail "the rebuilt zip doesn't run by its first line"

# What yt-dlp's --update leaves (a new build of sources) is compiled again.
cp -p "$WORK/plain" "$APP"
out=$(compile "$APP" 2>&1) || fail "a new build wasn't compiled: $out"
out=$(check_pyc "$APP" 2>&1) || fail "a new build: $out"

# Failures half-way: each leaves the file as it was, and no folder behind.
unchanged() {   # unchanged <what> <file> <sum> <mode> <compile's exit> <its output>
    [ "$5" = 1 ] || fail "$1: exit $5, not 1 ($6)"
    [ "$(sum "$2")" = "$3" ] || fail "$1: the file changed"
    [ "$(mode "$2")" = "$4" ] || fail "$1: the mode changed"
    [ "$(leftovers "$(dirname "$2")")" = 0 ] || fail "$1: a folder was left behind"
    case "$6" in *"left as it was"*) ;; *) fail "$1: no word of it: $6" ;; esac
}
# A damaged zip (cut short, as by a power cut during the update).
size=$(stat -c %s "$WORK/plain")
head -c "$(( size / 2 ))" "$WORK/plain" > "$APP"
was=$(sum "$APP")
out=$(compile "$APP" 2>&1); code=$?; unchanged "a damaged zip" "$APP" "$was" 751 "$code" "$out"
# A source this Python doesn't compile: unpacked, then compileall fails.
cp -p "$WORK/plain" "$APP"
"$PY" -I - "$APP" << 'PY'
import sys, zipfile
with zipfile.ZipFile(sys.argv[1], "a") as zf:
    zf.writestr("standin/extra.py", "def (\n")
PY
was=$(sum "$APP")
out=$(compile "$APP" 2>&1); code=$?; unchanged "a source that doesn't compile" "$APP" "$was" 751 "$code" "$out"
# The new zip built but not running: a Python that fails it.
cp -p "$WORK/plain" "$APP"
cat > "$WORK/failing-python" << SH
#!/bin/sh
for a; do case "\$a" in */.yt-dlp-compile-*/yt-dlp) exit 3 ;; esac; done
exec "$PY" "\$@"
SH
chmod 755 "$WORK/failing-python"
was=$(sum "$APP")
out=$(compile --python "$WORK/failing-python" "$APP" 2>&1); code=$?; unchanged "a new zip that doesn't run" "$APP" "$was" 751 "$code" "$out"
# A folder it can't write: as a user without root's way past its mode.
cp -p "$WORK/plain" "$APP"
was=$(sum "$APP")
chmod 555 "$WORK/bin"
if [ "$(id -u)" != 0 ]; then
    out=$(compile "$APP" 2>&1); code=$?; unchanged "a folder it can't write" "$APP" "$was" 751 "$code" "$out"
elif command -v setpriv > /dev/null 2>&1 && chmod -R a+rX "$WORK" && chmod a+x "$(dirname "$WORK")" 2> /dev/null \
        && setpriv --reuid=65534 --regid=65534 --clear-groups test -r "$APP" 2> /dev/null \
        && setpriv --reuid=65534 --regid=65534 --clear-groups test -r "$COMPILE" 2> /dev/null; then
    was_mode=$(mode "$APP")
    out=$(setpriv --reuid=65534 --regid=65534 --clear-groups "$PY" -I "$COMPILE" "$APP" 2>&1); code=$?
    unchanged "a folder it can't write" "$APP" "$was" "$was_mode" "$code" "$out"
else
    echo "SKIP: a folder it can't write (root, and nobody can't reach the test's folder)"
fi
chmod 755 "$WORK/bin"
# A game started: stopped, the file as it was, and not a failure.
touch "$WORK/playing"
out=$(compile --stop-if "$WORK/playing" "$APP" 2>&1); code=$?
[ "$code" = 0 ] || fail "a game's flag: exit $code, not 0 ($out)"
[ "$(sum "$APP")" = "$was" ] || fail "a game's flag: the file changed"
case "$out" in *stopped*) ;; *) fail "a game's flag: no word of the stop: $out" ;; esac
rm -f "$WORK/playing"
[ "$(leftovers "$WORK/bin")" = 0 ] || fail "a folder was left behind"
# A folder of a run cut short long ago is cleared by the next.
mkdir "$WORK/bin/.yt-dlp-compile-old"
touch -d '2 hours ago' "$WORK/bin/.yt-dlp-compile-old"
out=$(compile "$APP" 2>&1) || fail "the rebuild after a run cut short failed: $out"
[ "$(leftovers "$WORK/bin")" = 0 ] || fail "an old run's folder wasn't cleared"

# The stage: installed root's, outside the user's folders, and run in the
# image on its copy before that is given to the user; the unit runs it after
# each update, not failing for it, stopped by the flag its condition reads.
# shellcheck disable=SC2016  # the stage's own variables, as written
grep -q '^install -D -m 755 files/yt-dlp-compile "${ROOTFS_DIR}/usr/lib/osdos/yt-dlp-compile"$' "$STAGE/00-run.sh" \
    || fail "00-run.sh doesn't install yt-dlp-compile into /usr/lib/osdos"
# shellcheck disable=SC2016
run_line=$(grep -n '^/usr/lib/osdos/yt-dlp-compile "${DATA_BIN}/yt-dlp"$' "$STAGE/00-run.sh" | cut -d: -f1)
chown_line=$(grep -n '^chown -R ' "$STAGE/00-run.sh" | cut -d: -f1)
[ -n "$run_line" ] || fail "00-run.sh doesn't compile the image's yt-dlp"
if [ -n "$run_line" ] && [ -n "$chown_line" ] && [ "$run_line" -gt "$chown_line" ]; then
    fail "00-run.sh compiles after giving the folder to the user"
fi
post=$(sed -n 's/^ExecStartPost=//p' "$UNIT")
flag=$(sed -n 's/^ExecCondition=\/usr\/bin\/test ! -e //p' "$UNIT")
start=$(sed -n 's/^ExecStart=\([^ ]*\) --update$/\1/p' "$UNIT")
[ "$post" = "-/usr/lib/osdos/yt-dlp-compile --stop-if $flag $start" ] \
    || fail "the unit's ExecStartPost is '$post'"
grep -q '^User=' "$UNIT" || fail "the unit runs as root"

if [ "$fails" -ne 0 ]; then
    echo "$fails failed"
    exit 1
fi
echo "all passed"
