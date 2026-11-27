#!/usr/bin/env bash
# yt-dlp's zip rebuilt with its bytecode inside (os/stage-osdos/06-youtube:
# yt-dlp-compile), on a stand-in zipapp made here: it runs and prints the
# same, holds a .pyc beside every .py, keeps its first line and mode, imports
# the .pyc without a look at the source (and the source when a .pyc isn't
# this Python's), is left alone once compiled and survives being rebuilt
# twice; and a rebuild that fails half-way (a damaged zip, a source that
# doesn't compile, a new zip that doesn't run, a folder it can't write) or is
# stopped by a game leaves the file as it was. Then the update (--update) on
# a second stand-in with a module it imports late: a yt-dlp running from the
# file meanwhile finishes, the new build goes in once it has ended, a spare
# outliving the wait is put in place by the next run, leftovers of a run cut
# short are removed, nothing is swapped when nothing changed. Then the stage
# and the update unit running it. Needs Python 3; no network.
#   yt_dlp_compile_test.sh <os/stage-osdos/06-youtube>
set -u

STAGE="$1"
COMPILE="$STAGE/files/yt-dlp-compile"
UNIT="$STAGE/files/osdos-yt-dlp-update.service"
fails=0
fail() { echo "FAIL: $1"; fails=$((fails + 1)); }

PY=$(command -v python3) || { echo "SKIP: no python3"; exit 0; }
WORK=$(mktemp -d)
# A stand-in or an update still running after a failure is stopped first.
trap 'kill $(jobs -p) 2> /dev/null; wait 2> /dev/null; chmod -R u+w "$WORK" 2> /dev/null; rm -rf "$WORK"' EXIT

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
check_pyc() {   # check_pyc <zip> [<number of sources>]
    "$PY" -I - "$1" "${2:-3}" << 'PY'
import importlib.util, sys, zipfile
with zipfile.ZipFile(sys.argv[1]) as zf:
    names = zf.namelist()
    if len(names) != len(set(names)):
        sys.exit("an entry is in the zip twice")
    sources = [n for n in names if n.endswith(".py")]
    if len(sources) != int(sys.argv[2]):
        sys.exit(f"the zip has {len(sources)} sources, not {sys.argv[2]}")
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

# The update (--update): never a change to the file under a yt-dlp running
# from it. A second stand-in, as yt-dlp's zip is in this: its --update writes
# the "latest build" (upstream/yt-dlp, its version in upstream/version) over
# the file it runs from, whatever its name, as yt-dlp's zip does, and fails
# while upstream/fail is there (no network); "slow FLAG" imports a module
# only once FLAG appears, as yt-dlp imports most of its own as it needs them.
U="$WORK/upd"
mkdir -p "$U/upstream" "$U/bin"
standin() {   # standin <version> <out> [pad]: a build of the stand-in
    rm -rf "$U/src"
    mkdir -p "$U/src/standin"
    printf 'import standin.main\nstandin.main.run()\n' > "$U/src/__main__.py"
    : > "$U/src/standin/__init__.py"
    # A module before the lazy one, of a size of each build's own, so that
    # the lazy one is at another place in each build's zip.
    "$PY" -I -c "import sys; print('PAD = %r' % ('x' * int(sys.argv[1])))" "${3:-10}" > "$U/src/standin/apad.py"
    echo 'WORD = "lazy ok"' > "$U/src/standin/lazy.py"
    sed -e "s|@VERSION@|$1|" -e "s|@UPSTREAM@|$U/upstream|" > "$U/src/standin/main.py" << 'PY'
import os, sys
import standin.apad
VERSION = "@VERSION@"
UPSTREAM = "@UPSTREAM@"
def own_file():
    return os.path.realpath(os.path.join(os.path.dirname(__file__), ".."))
def run():
    args = sys.argv[1:]
    if args == ["--version"]:
        print(VERSION)
    elif args == ["--update"]:
        with open(os.path.join(UPSTREAM, "calls"), "a") as f:
            f.write(own_file() + "\n")
        if os.path.exists(os.path.join(UPSTREAM, "fail")):
            print("ERROR: Unable to obtain version info")
            sys.exit(1)
        with open(os.path.join(UPSTREAM, "version")) as f:
            latest = f.read().strip()
        if latest == VERSION:
            print(f"yt-dlp is up to date ({VERSION})")
            return
        with open(os.path.join(UPSTREAM, "yt-dlp"), "rb") as f:
            new = f.read()
        with open(own_file(), "wb") as f:
            f.write(new)
        print(f"Updated yt-dlp to {latest}")
    elif args[:1] == ["slow"]:
        import time
        print("waiting", flush=True)
        while not os.path.exists(args[1]):
            time.sleep(0.05)
        import standin.lazy
        print(standin.lazy.WORD)
    else:
        print("stand-in ran with", args)
PY
    "$PY" -I -m zipapp "$U/src" -p '/usr/bin/env python3' -o "$2" || { echo "FAIL: zipapp"; exit 1; }
}
standin 2026.11.20 "$U/v1" 10
standin 2026.11.26 "$U/upstream/yt-dlp" 30000
echo 2026.11.26 > "$U/upstream/version"
chmod 751 "$U/v1"
FILE="$U/bin/yt-dlp"
spares() { find "$U/bin" -mindepth 1 -maxdepth 1 ! -name yt-dlp | wc -l; }
ino() { stat -c %i "$1"; }
update() { compile --update "$@" "$FILE"; }
# Waits (up to 30 s) for a line in a file.
until_line() {
    local n=0
    until grep -q "$1" "$2" 2> /dev/null; do
        n=$((n + 1))
        [ "$n" -lt 300 ] || return 1
        sleep 0.1
    done
}
# A yt-dlp at work from the file: started as mpv's hook starts it, by its
# first line (or as given), and past its first imports before it returns.
start_user() {   # start_user <go flag> <out> [command...]
    local go=$1 out=$2
    shift 2
    [ $# -gt 0 ] || set -- "$FILE"
    rm -f "$go"
    PATH="$(dirname "$PY"):$PATH" "$@" slow "$go" > "$out" 2>&1 &
    user_pid=$!
    until_line waiting "$out" || fail "the stand-in didn't start: $(cat "$out")"
}

# The fault, as it was (the unit's old steps, "yt-dlp --update" then
# yt-dlp-compile over the file): a yt-dlp running meanwhile fails at its next
# import. The stand-in shows it, or the cases below would prove nothing.
cp -p "$U/v1" "$FILE"
compile "$FILE" > /dev/null 2>&1 || fail "the old steps: the first compile failed"
start_user "$U/go" "$U/old.out"
{ "$PY" -I "$FILE" --update > /dev/null 2>&1 && compile "$FILE" > /dev/null 2>&1; } \
    || fail "the old steps: the update or the compile failed"
touch "$U/go"
if wait "$user_pid"; then
    fail "the stand-in ran through a swap under it: $(cat "$U/old.out")"
fi
grep -q 'ZipImportError\|zipimport\|Error' "$U/old.out" || fail "the old steps: no import error: $(cat "$U/old.out")"

# Now: the same, through --update. The new build is made ready, waits while
# the process runs, and goes in once it has ended; the process finishes.
cp -p "$U/v1" "$FILE"
compile "$FILE" > /dev/null 2>&1 || fail "the first compile failed"
was=$(sum "$FILE")
start_user "$U/go" "$U/user.out"
: > "$U/upstream/calls"
update --wait 30 > "$U/update.log" 2>&1 &
update_pid=$!
until_line "waiting for process $user_pid" "$U/update.log" || fail "the update didn't wait for the process: $(cat "$U/update.log")"
[ "$(sum "$FILE")" = "$was" ] || fail "the file changed while a process ran from it"
[ -f "$U/bin/.yt-dlp-ready" ] || fail "no spare made ready while it waits"
[ "$(spares)" = 1 ] || fail "$(spares) spares while it waits, not 1"
grep -qx "$U/bin/.yt-dlp-update" "$U/upstream/calls" || fail "--update didn't run on the copy: $(cat "$U/upstream/calls")"
grep -qx "$FILE" "$U/upstream/calls" && fail "--update ran on the file itself"
touch "$U/go"
wait "$user_pid" || fail "the running yt-dlp failed: $(cat "$U/user.out")"
[ "$(tail -n 1 "$U/user.out")" = "lazy ok" ] || fail "the running yt-dlp printed: $(cat "$U/user.out")"
wait "$update_pid" || fail "the update failed: $(cat "$U/update.log")"
grep -q "replaced by the new build" "$U/update.log" || fail "no swap: $(cat "$U/update.log")"
[ "$("$PY" -I "$FILE" --version)" = 2026.11.26 ] || fail "the file isn't the new build"
out=$(check_pyc "$FILE" 5 2>&1) || fail "the new build: $out"
[ "$(mode "$FILE")" = 751 ] || fail "the new build's mode is $(mode "$FILE"), not 751"
[ "$(spares)" = 0 ] || fail "the update left $(spares) files beside the file"

# Nothing new, compiled already: nothing swapped (the same inode), no spare.
was=$(sum "$FILE"); was_ino=$(ino "$FILE")
out=$(update 2>&1) || fail "an update with nothing new failed: $out"
case "$out" in *"nothing to swap"*) ;; *) fail "an update with nothing new: no word of it: $out" ;; esac
{ [ "$(sum "$FILE")" = "$was" ] && [ "$(ino "$FILE")" = "$was_ino" ]; } || fail "an update with nothing new replaced the file"
[ "$(spares)" = 0 ] || fail "an update with nothing new left $(spares) files"

# A new build of sources there already (an update by hand): compiled, then
# swapped.
cp -p "$U/upstream/yt-dlp" "$FILE"
out=$(update 2>&1) || fail "the update after one by hand failed: $out"
out=$(check_pyc "$FILE" 5 2>&1) || fail "the update after one by hand: $out"

# The update fails (no network): the file as it was, no copy, exit 1.
cp -p "$U/v1" "$FILE"
was=$(sum "$FILE")
touch "$U/upstream/fail"
out=$(update 2>&1); code=$?
[ "$code" = 1 ] || fail "a failed update: exit $code, not 1 ($out)"
[ "$(sum "$FILE")" = "$was" ] || fail "a failed update changed the file"
[ "$(spares)" = 0 ] || fail "a failed update left $(spares) files"
rm -f "$U/upstream/fail"

# A process outliving the bound, by a symbolic link and a relative path: the
# file as it was, the spare kept; the next run swaps it with no update and no
# compile.
cp -p "$U/v1" "$FILE"
was=$(sum "$FILE")
ln -sf "$FILE" "$U/link"
rm -f "$U/go2"
(cd "$U" && exec "$PY" -I link slow "$U/go2") > "$U/user2.out" 2>&1 &
user_pid=$!
until_line waiting "$U/user2.out" || fail "the stand-in didn't start: $(cat "$U/user2.out")"
out=$(update --wait 2 2>&1); code=$?
[ "$code" = 0 ] || fail "past the bound: exit $code, not 0 ($out)"
case "$out" in *"still in use"*) ;; *) fail "past the bound: no word of it: $out" ;; esac
[ "$(sum "$FILE")" = "$was" ] || fail "past the bound: the file changed under the process"
{ [ -f "$U/bin/.yt-dlp-ready" ] && [ "$(spares)" = 1 ]; } || fail "past the bound: the spare wasn't kept alone ($(spares))"
spare=$(sum "$U/bin/.yt-dlp-ready")
touch "$U/go2"
wait "$user_pid" || fail "past the bound: the running yt-dlp failed: $(cat "$U/user2.out")"
: > "$U/upstream/calls"
touch "$U/upstream/fail"   # a download now would fail
out=$(update 2>&1) || fail "the run after the bound failed: $out"
rm -f "$U/upstream/fail"
[ "$(sum "$FILE")" = "$spare" ] || fail "the run after the bound didn't put the spare in place: $out"
[ -s "$U/upstream/calls" ] && fail "the run after the bound updated again"
case "$out" in *"compiled for"*) fail "the run after the bound compiled again: $out" ;; esac
[ "$(spares)" = 0 ] || fail "the run after the bound left $(spares) files"

# A game started while it waits: stopped, the file as it was, the spare kept
# and put in place by the next run.
cp -p "$U/v1" "$FILE"
was=$(sum "$FILE")
start_user "$U/go" "$U/user3.out"
rm -f "$WORK/playing"
update --wait 30 --stop-if "$WORK/playing" > "$U/update3.log" 2>&1 &
update_pid=$!
until_line "waiting for process" "$U/update3.log" || fail "the update didn't wait: $(cat "$U/update3.log")"
touch "$WORK/playing"
wait "$update_pid"; code=$?
[ "$code" = 0 ] || fail "a game while it waits: exit $code, not 0"
grep -q stopped "$U/update3.log" || fail "a game while it waits: no word of the stop: $(cat "$U/update3.log")"
[ "$(sum "$FILE")" = "$was" ] || fail "a game while it waits: the file changed"
[ "$(spares)" = 1 ] || fail "a game while it waits: $(spares) spares, not 1"
touch "$U/go"
wait "$user_pid" || fail "a game while it waits: the running yt-dlp failed: $(cat "$U/user3.out")"
# Its flag there from the start: nothing done.
out=$(update --stop-if "$WORK/playing" 2>&1); code=$?
{ [ "$code" = 0 ] && [ "$(sum "$FILE")" = "$was" ]; } || fail "a game's flag: exit $code or the file changed ($out)"
rm -f "$WORK/playing"
out=$(update 2>&1) || fail "the run after a game failed: $out"
{ [ "$("$PY" -I "$FILE" --version)" = 2026.11.26 ] && [ "$(spares)" = 0 ]; } || fail "the run after a game didn't swap: $out"
# A flag before the copy is updated: no copy left.
cp -p "$U/v1" "$FILE"
was=$(sum "$FILE")
touch "$WORK/playing"
out=$(update --stop-if "$WORK/playing" 2>&1); code=$?
{ [ "$code" = 0 ] && [ "$(sum "$FILE")" = "$was" ] && [ "$(spares)" = 0 ]; } \
    || fail "a game's flag at the start: exit $code, $(spares) files, or the file changed ($out)"
rm -f "$WORK/playing"

# Left by a run cut short: a copy half made, a compile's folder, are removed
# (never run, never put in place); a spare older than the file (an update by
# hand since) or that doesn't run is removed, the file left as it is.
cp -p "$U/upstream/yt-dlp" "$FILE"
compile "$FILE" > /dev/null 2>&1
was=$(sum "$FILE")
head -c 1000 "$FILE" > "$U/bin/.yt-dlp-update"
mkdir "$U/bin/.yt-dlp-compile-cut"
out=$(update 2>&1) || fail "the run after one cut short failed: $out"
{ [ "$(sum "$FILE")" = "$was" ] && [ "$(spares)" = 0 ]; } || fail "a run cut short: $(spares) files left, or the file changed ($out)"
cp -p "$U/v1" "$U/bin/.yt-dlp-ready"
touch -d '1 hour ago' "$U/bin/.yt-dlp-ready"
out=$(update 2>&1) || fail "the run with an old spare failed: $out"
{ [ "$(sum "$FILE")" = "$was" ] && [ "$(spares)" = 0 ]; } || fail "an old spare: put in place, or kept ($out)"
head -c 1000 "$FILE" > "$U/bin/.yt-dlp-ready"
out=$(update 2>&1) || fail "the run with a damaged spare failed: $out"
{ [ "$(sum "$FILE")" = "$was" ] && [ "$(spares)" = 0 ]; } || fail "a damaged spare: put in place, or kept ($out)"

# Only one run at a time: a second, while the first holds the folder, leaves
# it to the first.
cp -p "$U/v1" "$FILE"
start_user "$U/go5" "$U/user5.out"
update --wait 30 > "$U/update4.log" 2>&1 &
update_pid=$!
until_line "waiting for process" "$U/update4.log" || fail "the first run didn't wait: $(cat "$U/update4.log")"
out=$(update 2>&1) || fail "a second run failed: $out"
case "$out" in *"another run"*) ;; *) fail "a second run didn't leave it to the first: $out" ;; esac
touch "$U/go5"
wait "$user_pid" || fail "the yt-dlp of the first run's wait failed: $(cat "$U/user5.out")"
wait "$update_pid" || fail "the first run failed: $(cat "$U/update4.log")"
grep -qx "lazy ok" "$U/user5.out" || fail "the yt-dlp of the first run's wait failed: $(cat "$U/user5.out")"
[ "$("$PY" -I "$FILE" --version)" = 2026.11.26 ] || fail "the first run didn't swap"

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
# The update and the compile in one step, on a copy (--update): no bare
# "yt-dlp --update" over the file, nothing after it that replaces the file.
flag=$(sed -n 's/^ExecCondition=\/usr\/bin\/test ! -e //p' "$UNIT")
start=$(sed -n 's/^ExecStart=//p' "$UNIT")
# shellcheck disable=SC2016  # the unit's own placeholder, as written
[ "$start" = "/usr/lib/osdos/yt-dlp-compile --update --stop-if $flag"' /home/@FIRST_USER_NAME@/.local/share/OSD-OS/bin/yt-dlp' ] \
    || fail "the unit's ExecStart is '$start'"
[ -n "$flag" ] || fail "the unit has no game's flag in its condition"
! grep -q '^ExecStartPost=' "$UNIT" || fail "the unit still runs something after the update"
grep -q '^User=' "$UNIT" || fail "the unit runs as root"

if [ "$fails" -ne 0 ]; then
    echo "$fails failed"
    exit 1
fi
echo "all passed"
