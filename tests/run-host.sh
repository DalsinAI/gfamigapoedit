#!/bin/sh
# Host checks for our own pieces, no Amiga needed:
#  - the pugixml layer on OXP gives the same output as pugixml itself (fetched
#    here only to compare with, never built into the port)
#   sh tests/run-host.sh        (needs g++, gcc, git)
# Copyright (c) 2026 AmiCradle Team
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/.." && pwd)
W=${WORK:-$(mktemp -d)}
gitpin() { [ -d "$3/.git" ] || { mkdir -p "$3"; git -C "$3" init -q; git -C "$3" fetch -q --depth 1 "$1" "$2"; git -C "$3" checkout -q FETCH_HEAD; }; }
gitpin https://github.com/DalsinAI/gfopenxml f5cad87472de931b71b2fa18a1f121da032a6bac "$W/gfopenxml"
gitpin https://github.com/zeux/pugixml v1.15 "$W/pugixml"
X="$W/gfopenxml/oxp"
gcc -O1 -c -I"$X/include" "$X/src/oxp.c" -o "$W/oxp.o"
g++ -std=gnu++17 -O1 -I"$W/pugixml/src" "$HERE/pugi-compare.cpp" "$W/pugixml/src/pugixml.cpp" -o "$W/ref"
g++ -std=gnu++17 -O1 -I"$ROOT/src/pugi" -I"$X/include" "$HERE/pugi-compare.cpp" "$ROOT/src/pugi/pugixml.cpp" "$W/oxp.o" -o "$W/ours"
"$W/ref" > "$W/ref.txt"; "$W/ours" > "$W/ours.txt"
# the one difference on purpose: an entity the document's DTD declares is
# expanded by OXP; pugixml leaves "&e;" and saves it as "&amp;e;"
if diff "$W/ref.txt" "$W/ours.txt" | grep '^[<>]' | grep -v '<value>&amp;e; A ©</value>\|<value>ent A ©</value>'; then
    echo "pugixml layer: output differs"; exit 1
fi
echo "pugixml layer: same output as pugixml 1.15"
