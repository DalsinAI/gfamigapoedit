#!/bin/sh
# Builds Poedit for AmigaOS 3 (68040) on the GFOpenWX Toolkit, with a stove:
# the libraries (scripts/build-deps.sh), the toolkit SDK, then Poedit.
#   STOVE=.../stoves/os32-gcc16-open6 OUT=out ./build-amiga.sh
# Each stage leaves a stamp in the work tree, which lives outside the
# checkout (Cradle cleans the checkout before every job), so a run that hits
# the time cap goes on from where it stopped next time.
# Poedit and the libraries are fetched at the versions poedit.lock gives;
# nothing third-party is kept here.
# Copyright (c) 2026 AmiCradle Team
set -eu
# Daletop runs Dale's instances too: the build is niced and runs 4 jobs
# (the toolkit 12, as its own build does: its files are small)
renice -n 19 $$ >/dev/null 2>&1 || true
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
. "$HERE/poedit.lock"
STOVE=${STOVE:-"$HOME/AmiCradle/stoves/os32-gcc16-open6"}
WORK="${WORK:-$HOME/.cache/gfamigapoedit/build}"; OUT="${OUT:-$WORK/out}"; JOBS=${JOBS:-4}
TC="$STOVE/prefix"
mkdir -p "$WORK/deps" "$OUT"
t0=$(date +%s)
left() { echo $(( ${BUDGET_TOTAL:-840} - ($(date +%s) - t0) )); }

fetch() {
    d="$WORK/deps/$1"
    if [ "$(git -C "$d" rev-parse HEAD 2>/dev/null)" != "$3" ]; then
        rm -rf "${d:?}"; mkdir -p "$d"; git -C "$d" init -q
        git -C "$d" fetch -q --depth 1 "$2" "$3"; git -C "$d" checkout -q FETCH_HEAD
    fi
}
fetch poedit "$POEDIT_GIT" "$POEDIT_COMMIT"
echo "poedit: $(git -C "$WORK/deps/poedit" log --oneline -1)"

# the libraries
TOOLCHAIN="$TC" WORK="$WORK" JOBS=$JOBS sh "$HERE/scripts/build-deps.sh" > "$WORK.deps.log" 2>&1 \
    || { grep '^stage ' "$WORK.deps.log"; grep '^ERR ' "$WORK.deps.log" | cut -c1-500; grep -v '^ \|warning\|^stage \|^ERR ' "$WORK.deps.log" | cut -c1-240 | tail -15; exit 1; }
grep "^stage " "$WORK.deps.log" || true
echo "libraries: $(( $(date +%s) - t0 )) s"

# the toolkit SDK: a lab SDK another job already built (WXT_SDK_JOB, its
# gfopenwx-sdk.tar.gz checked by WXT_SDK_SHA256), or built here, again only
# when its pinned commit changes
WXB="$WORK/gfopenwx"
if [ -n "${WXT_SDK_JOB:-}" ]; then
    T="${CRADLE_JOBS:-$HOME/AmiCradle-dev/cradle-builds/jobs}/$WXT_SDK_JOB/out/gfopenwx-sdk.tar.gz"
    if [ "$(cat "$WXB/out/.rev" 2>/dev/null)" != "$WXT_SDK_SHA256" ]; then
        echo "$WXT_SDK_SHA256  $T" | sha256sum -c - >/dev/null || { echo "toolkit SDK: $T missing or not $WXT_SDK_SHA256"; exit 1; }
        rm -rf "$WXB/out" "$WXB/sdk-unpack"; mkdir -p "$WXB/out" "$WXB/sdk-unpack"
        tar -C "$WXB/sdk-unpack" -xzf "$T"
        c=$(find "$WXB/sdk-unpack" -path '*/bin/wx-config' | head -1)
        [ -n "$c" ] || { echo "toolkit SDK: no bin/wx-config in $T"; exit 1; }
        mv "$(dirname "$(dirname "$c")")" "$WXB/out/sdk"; rm -rf "$WXB/sdk-unpack"
        echo "$WXT_SDK_SHA256" > "$WXB/out/.rev"
    fi
    WXT_COMMIT=$WXT_SDK_SHA256
else
    fetch gfopenwxtoolkit "$WXT_REPO" "$WXT_COMMIT"
fi
if [ ! -x "$WXB/out/sdk/bin/wx-config" ] || [ "$(cat "$WXB/out/.rev" 2>/dev/null)" != "$WXT_COMMIT" ]; then
    [ "$(left)" -gt 300 ] || { echo "toolkit next run ($(left) s left)"; exit 0; }
    # stopped before the job's cap so the log comes back; make goes on
    # from its objects next run
    rc=0
    STOVE="$STOVE" WORK="$WXB" OUT="$WXB/out" JOBS=${WX_JOBS:-12} timeout $(( $(left) - 20 )) sh "$WORK/deps/gfopenwxtoolkit/build-amiga.sh" > "$WORK.wx.log" 2>&1 || rc=$?
    if [ "$rc" = 124 ]; then
        echo "toolkit: stopped at the time cap, $(ls "$WXB/gui/obj"/*.o 2>/dev/null | wc -l) GUI objects so far; goes on next run"
        grep -v '^m68k-amigaos-g\|warning\|^ \|^In file\|duplicate section' "$WORK.wx.log" | tail -12 | cut -c1-300
        # where the time went
        for f in "$WXB"/wxWidgets-*/.patched "$WXB/base/.configured" "$WXB/wxgui/.toolkit" "$WXB/gui/wx/setup.h"; do
            echo "toolkit file: $(ls -l --time-style=+%d-%T "$f" 2>/dev/null | awk '{print $6, $7}')"
        done
        echo "toolkit gui objects: oldest $(ls -tr --time-style=+%T -l "$WXB/gui/obj"/*.o 2>/dev/null | head -1 | awk '{print $6}'), newest $(ls -t --time-style=+%T -l "$WXB/gui/obj"/*.o 2>/dev/null | head -1 | awk '{print $6}')"
        echo "toolkit base objects: $(find "$WXB/base" -name '*.o' -newer "$WORK.deps.log" | wc -l) made this run of $(find "$WXB/base" -name '*.o' | wc -l)"
        echo "toolkit log: $(grep -c 'm68k-amigaos-g' "$WORK.wx.log") compiler lines, $(wc -l < "$WORK.wx.log") lines"
        exit 0
    fi
    [ "$rc" = 0 ] || { tail -30 "$WORK.wx.log" | cut -c1-400; exit 1; }
    [ -x "$WXB/out/sdk/bin/wx-config" ] && echo "$WXT_COMMIT" > "$WXB/out/.rev"
    echo "toolkit: $(( $(date +%s) - t0 )) s"
fi
[ -x "$WXB/out/sdk/bin/wx-config" ] || { tail -30 "$WORK.wx.log"; echo "no toolkit SDK yet (run again)"; exit 0; }
ls -l "$WORK/sysroot/lib" > "$OUT/libraries.txt"

# Poedit
[ "$(left)" -gt 120 ] || { echo "Poedit next run ($(left) s left)"; exit 0; }
rc=0
# stopped before the job's cap (a compiler started just before the
# deadline can run for minutes) so the log comes back; the objects stay
TOOLCHAIN="$TC" WORK="$WORK" WX="$WXB/out/sdk" OUT="$OUT" JOBS=${POEDIT_JOBS:-12} CC_DEADLINE=$(( t0 + ${BUDGET_TOTAL:-840} - 240 )) \
    timeout $(( $(left) - 25 )) sh "$HERE/scripts/build-poedit.sh" > "$WORK.poedit.log" 2>&1 || rc=$?
grep '^stage ' "$WORK.poedit.log" || true
poedit_progress() {
    echo "Poedit: $(ls "$WORK/obj/poedit"/*.o 2>/dev/null | wc -l) objects so far, goes on next run"
    # the first errors from files that failed this run
    for e in $(ls -t "$WORK/obj/poedit"/*.err 2>/dev/null); do
        [ -s "$e" ] && [ ! -f "${e%.err}" ] && grep -q 'error' "$e" && { echo "FAIL $(basename "${e%.o.err}")"; grep -m 12 'error' "$e" | cut -c1-300 | sed 's/^/ERR /'; }
    done | head -60
}
case $rc in
    0) ls -l "$OUT/Poedit" ;;
    3|124) poedit_progress ;;
    *) grep '^ERR \|^FAIL \|undefined' "$WORK.poedit.log" | cut -c1-400 | head -60; tail -5 "$WORK.poedit.log" | cut -c1-300; exit 1 ;;
esac
WORK="$WORK" OUT="$OUT" HERE="$HERE" sh "$HERE/scripts/lab-kit.sh" || true
echo "total: $(( $(date +%s) - t0 )) s"
