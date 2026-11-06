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
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
. "$HERE/poedit.lock"
STOVE=${STOVE:-"$HOME/AmiCradle/stoves/os32-gcc16-open6"}
WORK="${WORK:-$HOME/.cache/gfamigapoedit/build}"; OUT="${OUT:-$WORK/out}"; JOBS=${JOBS:-12}
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

# ICU (a host build for its tools, then the cross build)
if [ "$(cat "$WORK/.stamp-icu" 2>/dev/null)" != "$ICU_SHA256-$OB_COMMIT" ]; then
    [ "$(left)" -gt 420 ] || { echo "ICU next run ($(left) s left)"; exit 0; }
    TOOLCHAIN="$TC" WORK="$WORK" JOBS=$JOBS sh "$HERE/scripts/build-icu.sh" > "$WORK.icu.log" 2>&1 \
        || { cut -c1-300 "$WORK.icu.log" | tail -40; exit 1; }
    grep '^stage ' "$WORK.icu.log" || true
    echo "ICU: $(( $(date +%s) - t0 )) s"
fi

# the toolkit SDK, again only when its pinned commit changes
fetch gfopenwxtoolkit "$WXT_REPO" "$WXT_COMMIT"
WXB="$WORK/gfopenwx"
if [ ! -x "$WXB/out/sdk/bin/wx-config" ] || [ "$(cat "$WXB/out/.rev" 2>/dev/null)" != "$WXT_COMMIT" ]; then
    [ "$(left)" -gt 600 ] || { echo "toolkit next run ($(left) s left)"; exit 0; }
    STOVE="$STOVE" WORK="$WXB" OUT="$WXB/out" JOBS=$JOBS sh "$WORK/deps/gfopenwxtoolkit/build-amiga.sh" > "$WORK.wx.log" 2>&1 \
        || { tail -30 "$WORK.wx.log"; exit 1; }
    [ -x "$WXB/out/sdk/bin/wx-config" ] && echo "$WXT_COMMIT" > "$WXB/out/.rev"
    echo "toolkit: $(( $(date +%s) - t0 )) s"
fi
[ -x "$WXB/out/sdk/bin/wx-config" ] || { tail -30 "$WORK.wx.log"; echo "no toolkit SDK yet (run again)"; exit 0; }
"$WXB/out/sdk/bin/wx-config" --libs | head -c 400; echo
ls -l "$WORK/sysroot/lib" | tee "$OUT/libraries.txt"
echo "total: $(( $(date +%s) - t0 )) s"
