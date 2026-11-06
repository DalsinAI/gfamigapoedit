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
# and our patches (patches/NNNN-*.patch, paths a/src/...) on a fresh copy
# the compilers' scratch files beside the build, not in the shared /tmp
export TMPDIR="$WORK/tmp"; rm -rf "$TMPDIR"; mkdir -p "$TMPDIR"
S="$WORK/poedit-src"
# json.h takes nlohmann from ../deps/json, next to the src copy
[ -e "$WORK/deps/json/single_include" ] || { mkdir -p "$WORK/deps"; ln -sfn "$WORK/json" "$WORK/deps/json"; }
REV="$POEDIT_COMMIT-$(cat "$HERE"/patches/[0-9]*.patch 2>/dev/null | sha256sum | cut -c1-16)"
if [ "$(cat "$S/.rev" 2>/dev/null)" != "$REV" ]; then
    rm -rf "$S"; cp -Rp "$POE/src" "$S"
    for p in "$HERE"/patches/[0-9]*.patch; do
        [ -f "$p" ] || continue
        (cd "$S" && patch -p2 -s --no-backup-if-mismatch < "$p") || { echo "FAIL patch $(basename "$p")"; exit 1; }
    done
    # a patched file is newer than its object, so cc_many builds it again
    echo "$REV" > "$S/.rev"
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
    -DBOOST_ALL_NO_LIB -DBOOST_THREAD_POSIX -DBOOST_THREAD_VERSION=4 -DBOOST_THREAD_PROVIDES_EXECUTORS -Wno-deprecated-declarations"
# new flags or a new toolkit SDK: every object again
FH=$(printf '%s %s %s %s' "$FL" "$CF" "$GFBOOST_SHA256" "${WXT_SDK_SHA256:-$WXT_COMMIT}" | sha256sum | cut -c1-16)
[ "$(cat "$WORK/obj/poedit/.flags" 2>/dev/null)" = "$FH" ] || { rm -rf "$WORK/obj/poedit"; mkdir -p "$WORK/obj/poedit"; echo "$FH" > "$WORK/obj/poedit/.flags"; }
# shellcheck disable=SC2086
cc_many "$WORK/obj/poedit" "$S" "$FL" $SRCS

# the program
O="$WORK/obj/poedit"
mkdir -p "$OUT"
# shellcheck disable=SC2046
m68k-amigaos-g++ -noixemul -m68040 -specs="$SPECS" -o "$OUT/Poedit.debug" "$O"/*.o \
    -L"$SYS/lib" -loxp -ltransmem -licuuc -lboost_thread -lboost_filesystem -lboost_iostreams -lboost_date_time -lboost_chrono -lboost_atomic \
    $("$WXC" --libs core,base,adv,html,xml,xrc,aui,net) -lpthread -latomic -lm \
    -Wl,-Map,"$WORK/poedit.map" > "$WORK/poedit.link.log" 2>&1 \
    || { echo "link failed: $(grep -c . "$WORK/poedit.link.log") log lines"
         grep -v 'duplicate section' "$WORK/poedit.link.log" | grep -E "undefined reference|error|cannot find|No such" | sed 's/.*undefined reference to/undefined:/' | sort | uniq -c | sort -rn | head -60 | sed 's/^/ERR /'
         grep -v 'duplicate section\|: in function\|undefined reference' "$WORK/poedit.link.log" | tail -15 | cut -c1-300 | sed 's/^/ERR /'; exit 1; }
cp "$OUT/Poedit.debug" "$OUT/Poedit"; m68k-amigaos-strip "$OUT/Poedit"
echo "stage poedit: $(ls -l "$OUT/Poedit" | awk '{print $5}') bytes"
