#!/bin/sh
# What a lab needs besides Poedit, gathered into OUT/kit/ for lab_put:
# transmem.library from GFOpenTransMem's release, and (probe for now) where
# Daletop already keeps unicode.library's ICU build and our other libraries.
#   WORK=... OUT=... HERE=... scripts/lab-kit.sh
# Copyright (c) 2026 AmiCradle Team
set -u
K="$OUT/kit"; rm -rf "$K"; mkdir -p "$K/Libs"
T=$(find "$WORK/gfopentransmem" -path '*/Libs/transmem.library' | head -1)
[ -n "$T" ] && cp "$T" "$K/Libs/" && echo "kit: transmem.library $(ls -l "$T" | awk '{print $5}') bytes"
echo "probe: $(date -u +%T)"
for d in "$HOME/AmigaChrome" "$HOME/AmiCradle-dev" "$HOME/AmiCradle" "$HOME/.cache" /run/media/da1ek/WorkingSpace/AmigaChrome /run/media/da1ek/DevDrive; do
    [ -d "$d" ] || continue
    timeout 60 find "$d" -maxdepth 7 \( -name 'icu78-m68k-amigaos' -o -name 'unicode.library' -o -name 'gfopenpng.datatype' -o -name 'gfopencrypto.library' -o -name 'icudt78b.dat' \) 2>/dev/null \
        | grep -v '/Instance-\|/labs/' | head -12 | while read f; do echo "probe: $(ls -ld --time-style=+%d%b-%H:%M "$f" | awk '{print $5, $6}') $f"; done
done
echo "probe: done $(date -u +%T)"
