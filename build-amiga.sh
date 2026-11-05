#!/bin/sh
# Copyright (c) 2026 AmiCradle Team
# Builds Poedit for AmigaOS 3 on the GFOpenWX Toolkit.
# Step 1: shows what the stove has for Poedit's needs (Boost, ICU, threads,
# gettext, build tools) and fetches Poedit at the commit poedit.lock pins.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
STOVE=${STOVE:-"$HOME/AmiCradle/stoves/os32-gcc16-open6"}
OUT="${OUT:-$HERE/build/out}"; WORK="${WORK:-$HERE/build}"
TC="$STOVE/prefix"
. "$HERE/poedit.lock"
mkdir -p "$WORK/deps" "$OUT"
echo "== tools"
for t in cmake autoconf automake libtool pkg-config python3 msgfmt bzip2 xz; do
    printf '%s: ' "$t"; command -v $t || echo none
done
echo "== stove prefix"
ls "$TC"; ls "$TC/m68k-amigaos" 2>/dev/null
echo "== headers and libraries"
for d in "$TC/m68k-amigaos/include" "$TC/m68k-amigaos/sys-include" "$TC/include"; do
    [ -d "$d" ] || continue
    echo "-- $d"; ls "$d" | tr '\n' ' '; echo
done
find "$TC" -maxdepth 6 \( -name 'pthread.h' -o -name 'libpthread*' -o -name 'libicu*' -o -name 'unicode' -o -name 'boost' -o -name 'libboost*' -o -name 'libintl*' -o -name 'libiconv*' -o -name 'iconv.h' -o -name 'libexpat*' -o -name 'libz.a' -o -name 'libstdc++.a' \) 2>/dev/null | head -60
echo "== Poedit"
d="$WORK/deps/poedit"
if [ "$(git -C "$d" rev-parse HEAD 2>/dev/null)" != "$POEDIT_COMMIT" ]; then
    rm -rf "${d:?}"; mkdir -p "$d"; git -C "$d" init -q
    git -C "$d" fetch -q --depth 1 "$POEDIT_GIT" "$POEDIT_COMMIT"; git -C "$d" checkout -q FETCH_HEAD
fi
git -C "$d" log --oneline -1
echo probe > "$OUT/probe.txt"
