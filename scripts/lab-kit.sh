#!/bin/sh
# What a lab needs besides Poedit, gathered into OUT/kit/ for lab_put:
# transmem.library from GFOpenTransMem's release (scripts/build-unicodelib.sh
# adds unicode.library).
#   WORK=... OUT=... HERE=... scripts/lab-kit.sh
# Copyright (c) 2026 AmiCradle Team
set -u
K="$OUT/kit"; rm -rf "$K"; mkdir -p "$K/Libs"
T=$(find "$WORK/gfopentransmem" -path '*/Libs/transmem.library' | head -1)
[ -n "$T" ] && cp "$T" "$K/Libs/" && echo "kit: transmem.library $(ls -l "$T" | awk '{print $5}') bytes"
