#!/bin/sh
# The README says the program needs gfopenlayout.library 12 or later, the
# version the toolkit's start-up code asks for (gfopenwxtoolkit PR 1).
HERE=$(cd "$(dirname "$0")/.." && pwd)
while [ ! -f "$HERE/README.md" ]; do HERE=$(dirname "$HERE"); done
fail=0
grep -q 'gfopenlayout.library` 12 or later' "$HERE/README.md" && echo "ok - README names gfopenlayout.library 12 or later" || { echo "not ok - README names gfopenlayout.library 12 or later"; fail=1; }
grep -q 'LIBS:' "$HERE/README.md" && echo "ok - README says where it goes (LIBS:)" || { echo "not ok - README says LIBS:"; fail=1; }
exit $fail
