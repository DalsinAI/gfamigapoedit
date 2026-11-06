#!/bin/sh
# Builds Poedit itself on the toolkit SDK and the libraries
# scripts/build-deps.sh made, with our pieces in place of Poedit's
# Lucene++ translation memory (src/tm) and pugixml (src/pugi, on OXP).
#   TOOLCHAIN=.../prefix WORK=build WX=.../sdk OUT=out scripts/build-poedit.sh
# Leaves OUT/Poedit. A run that reaches CC_DEADLINE stops with code 3 and
# goes on from there next time.
# Copyright (c) 2026 AmiCradle Team
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
. "$HERE/poedit.lock"
WORK=${WORK:-$HOME/.cache/gfamigapoedit/build}; JOBS=${JOBS:-4}
PATH="$TOOLCHAIN/bin:$PATH"
SYS="$WORK/sysroot"; POE="$WORK/deps/poedit"; WXC="$WX/bin/wx-config"
SPECS="$HERE/scripts/m68k-soft-m68040.specs"
CF="-noixemul -m68040 -O2 -fno-delete-null-pointer-checks -D_DEFAULT_SOURCE"
. "$HERE/scripts/cc-many.sh"

# Poedit's sources, with ours laid over them: when an overlay changes, cp
# gives it a new time and cc_many builds it again
S="$WORK/poedit-src"
if [ "$(cat "$S/.rev" 2>/dev/null)" != "$POEDIT_COMMIT" ]; then
    rm -rf "$S" "$WORK/obj/poedit"; cp -R "$POE/src" "$S"; echo "$POEDIT_COMMIT" > "$S/.rev"
fi
for f in tm/transmem.cpp pugi/pugixml.hpp pugi/pugixml.cpp; do
    mkdir -p "$S/$(dirname "$f")"
    cmp -s "$HERE/src/$f" "$S/$f" || cp "$HERE/src/$f" "$S/$f"
done
python3 "$HERE/scripts/gen-xrc.py" "$S/compiled_xrc.cpp.new" "$S/resources/manager.xrc" "$S/resources/menus.xrc" "$S/resources/properties.xrc"
cmp -s "$S/compiled_xrc.cpp.new" "$S/compiled_xrc.cpp" && rm "$S/compiled_xrc.cpp.new" || mv "$S/compiled_xrc.cpp.new" "$S/compiled_xrc.cpp"

# poedit_SOURCES from Poedit's src/Makefile.am, without the cloud accounts
SRCS=$(sed -n '/^poedit_SOURCES = /,/^[^ ]/p' "$S/Makefile.am" | tr ' \t\\' '\n\n\n' | grep '\.cpp$')
[ -n "$SRCS" ] || { echo "no sources in $S/Makefile.am"; exit 1; }
SRCS="$SRCS compiled_xrc.cpp pugi/pugixml.cpp"
echo "stage poedit sources: $(echo $SRCS | wc -w)"

# shellcheck disable=SC2046
FL="-std=gnu++17 $("$WXC" --cxxflags) -I$S -I$S/pugi -I$SYS/include \
    -DHAVE_PUGIXML -DwxNO_UNSAFE_WXSTRING_CONV=1 -DPOEDIT_PREFIX=\"PROGDIR:\" \
    -DBOOST_ALL_NO_LIB -DBOOST_THREAD_POSIX -DBOOST_THREAD_VERSION=4 -Wno-deprecated-declarations"
# shellcheck disable=SC2086
cc_many "$WORK/obj/poedit" "$S" "$FL" $SRCS

# the program
O="$WORK/obj/poedit"
mkdir -p "$OUT"
# shellcheck disable=SC2046
m68k-amigaos-g++ -noixemul -m68040 -specs="$SPECS" -o "$OUT/Poedit.debug" "$O"/*.o \
    -L"$SYS/lib" -lboost_poedit -loxp -lsqlite3 -licuuc \
    $("$WXC" --libs core,base,adv,html,xml,xrc,aui,net) -lz -lpthread -latomic -lm \
    -Wl,-Map,"$WORK/poedit.map" > "$WORK/poedit.link.log" 2>&1 \
    || { grep -E "undefined reference|error" "$WORK/poedit.link.log" | sed 's/.*undefined reference to/undefined:/' | sort | uniq -c | sort -rn | head -60; exit 1; }
cp "$OUT/Poedit.debug" "$OUT/Poedit"; m68k-amigaos-strip "$OUT/Poedit"
echo "stage poedit: $(ls -l "$OUT/Poedit" | awk '{print $5}') bytes"
