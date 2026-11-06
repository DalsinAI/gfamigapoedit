#!/bin/sh
# Builds what Poedit needs besides wx into one sysroot: the shared
# GFOpenBoost SDK's headers and libraries, nlohmann json,
# our OXP parser, SQLite, and the stubs that send ICU's calls to our
# unicode.library.
#   TOOLCHAIN=.../prefix WORK=build scripts/build-deps.sh
# Copyright (c) 2026 AmiCradle Team
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
. "$HERE/poedit.lock"
WORK=${WORK:-$HOME/.cache/gfamigapoedit/build}; JOBS=${JOBS:-12}
PATH="$TOOLCHAIN/bin:$PATH"
SYS="$WORK/sysroot"; D="$WORK/dl"
mkdir -p "$SYS/include" "$SYS/lib" "$D"
CXXF="-noixemul -m68040 -O2 -std=gnu++17 -fno-delete-null-pointer-checks -D_DEFAULT_SOURCE"
PD="$TOOLCHAIN/m68k-amigaos/port-deps"
CF="-noixemul -m68040 -O2 -fno-delete-null-pointer-checks -D_DEFAULT_SOURCE"
POE=${POEDIT_SRC:-$WORK/deps/poedit}

fetch() { # url sha file
    if [ ! -f "$D/$3" ] || ! echo "$2  $D/$3" | sha256sum -c - >/dev/null 2>&1; then
        curl -fsSL -o "$D/$3.part" "$1"
        echo "$2  $D/$3.part" | sha256sum -c - >/dev/null || { echo "sha256 mismatch: $1"; exit 1; }
        mv "$D/$3.part" "$D/$3"
    fi
}
stamp() { [ "$(cat "$WORK/.stamp-$1" 2>/dev/null)" = "$2" ]; }
gitpin() { # url commit dir
    if [ "$(git -C "$3" rev-parse HEAD 2>/dev/null)" != "$2" ]; then
        rm -rf "$3"; mkdir -p "$3"; git -C "$3" init -q
        git -C "$3" fetch -q --depth 1 "$1" "$2"; git -C "$3" checkout -q FETCH_HEAD
        [ "$(git -C "$3" rev-parse HEAD)" = "$2" ] || { echo "commit mismatch: $1"; exit 1; }
    fi
}
. "$HERE/scripts/cc-many.sh"

# Boost: the shared GFOpenBoost SDK (headers with its one Amiga config, and
# its libboost_<part>.a), never a Boost of our own
if ! stamp boost "$GFBOOST_SHA256"; then
    fetch "$GFBOOST_URL" "$GFBOOST_SHA256" gfopenboost-sdk.tar.xz
    rm -rf "$WORK/gfopenboost"; mkdir -p "$WORK/gfopenboost"
    tar -C "$WORK/gfopenboost" -xJf "$D/gfopenboost-sdk.tar.xz"
    G=$(dirname "$(find "$WORK/gfopenboost" -maxdepth 2 -name boost.txt | head -1)")
    [ -d "$G/include/boost" ] || { echo "gfopenboost: no include/boost in $GFBOOST_URL"; exit 1; }
    rm -rf "$SYS/include/boost" "$SYS/lib"/libboost_*.a
    cp -R "$G/include/boost" "$SYS/include/boost"
    cp "$G"/lib/libboost_*.a "$SYS/lib/"
    echo "$GFBOOST_SHA256" > "$WORK/.stamp-boost"
fi
echo "stage boost: gfopenboost $(head -1 "$(find "$WORK/gfopenboost" -maxdepth 2 -name boost.txt | head -1)" | cut -c1-60), $(ls "$SYS/lib"/libboost_*.a | wc -l) libraries"

# nlohmann json (Poedit's own pin; headers only)
if ! stamp json "$JSON_COMMIT"; then
    gitpin "$JSON_GIT" "$JSON_COMMIT" "$WORK/json"
    rm -rf "$SYS/include/nlohmann"; cp -R "$WORK/json/single_include/nlohmann" "$SYS/include/nlohmann"
    echo "$JSON_COMMIT" > "$WORK/.stamp-json"
fi
echo "stage json: headers"

# Our own XML parser, OXP (GFOpenXML), for the pugixml layer (src/pugi)
if ! stamp oxp "$OXP_COMMIT"; then
    gitpin "$OXP_REPO" "$OXP_COMMIT" "$WORK/gfopenxml"
    rm -rf "$WORK/obj/oxp"
    cc_many "$WORK/obj/oxp" "$WORK/gfopenxml/oxp" "-I$WORK/gfopenxml/oxp/include" src/oxp.c
    cp "$WORK/gfopenxml/oxp/include/oxp.h" "$SYS/include/"
    rm -f "$SYS/lib/liboxp.a"; m68k-amigaos-ar rcs "$SYS/lib/liboxp.a" "$WORK/obj/oxp"/*.o
    echo "$OXP_COMMIT" > "$WORK/.stamp-oxp"
fi
echo "stage oxp: $(ls -l "$SYS/lib/liboxp.a" | awk '{print $5}') bytes"

# SQLite, for the translation memory (src/tm): the copy Audacity keeps, as
# GFAmigaAudacity builds it, with our Amiga changes (patches/sqlite-amiga.patch)
if ! stamp sqlite "$SQLITE_C_SHA256-$(sha256sum "$HERE/patches/sqlite-amiga.patch" | cut -c1-16)"; then
    fetch "$SQLITE_URL/sqlite3.c" "$SQLITE_C_SHA256" sqlite3.c
    fetch "$SQLITE_URL/sqlite3.h" "$SQLITE_H_SHA256" sqlite3.h
    Q="$WORK/sqlite"; rm -rf "$Q" "$WORK/obj/sqlite"; mkdir -p "$Q"
    cp "$D/sqlite3.c" "$D/sqlite3.h" "$Q/"
    (cd "$Q" && patch -p1 -s < "$HERE/patches/sqlite-amiga.patch")
    cc_many "$WORK/obj/sqlite" "$Q" "-DSQLITE_DQS=0 -DSQLITE_DEFAULT_MEMSTATUS=0 -DSQLITE_DEFAULT_SYNCHRONOUS=1 \
        -DSQLITE_LIKE_DOESNT_MATCH_BLOBS -DSQLITE_MAX_EXPR_DEPTH=0 -DSQLITE_OMIT_DEPRECATED -DSQLITE_OMIT_SHARED_CACHE \
        -DSQLITE_USE_ALLOCA -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_OMIT_WAL -DSQLITE_MAX_MMAP_SIZE=0 -DSQLITE_TEMP_STORE=3 \
        -DSQLITE_THREADSAFE=1 -DHAVE_USLEEP=1" sqlite3.c
    cp "$Q/sqlite3.h" "$SYS/include/"
    rm -f "$SYS/lib/libsqlite3.a"; m68k-amigaos-ar rcs "$SYS/lib/libsqlite3.a" "$WORK/obj/sqlite"/*.o
    echo "$SQLITE_C_SHA256-$(sha256sum "$HERE/patches/sqlite-amiga.patch" | cut -c1-16)" > "$WORK/.stamp-sqlite"
fi
echo "stage sqlite: $(ls -l "$SYS/lib/libsqlite3.a" | awk '{print $5}') bytes"

# ICU's calls go to our unicode.library (GFOpenBrowser src/unicodelib):
# ICU's headers (from its release, headers only) and the library's stubs
if ! stamp unicode "$ICU_SHA256-$UL_COMMIT"; then
    fetch "$ICU_URL" "$ICU_SHA256" icu.tgz
    rm -rf "$WORK/icu-h" "$SYS/include/unicode"; mkdir -p "$WORK/icu-h" "$SYS/include/unicode"
    tar -C "$WORK/icu-h" -xzf "$D/icu.tgz" icu/source/common/unicode icu/source/i18n/unicode
    cp "$WORK/icu-h/icu/source/common/unicode/"*.h "$WORK/icu-h/icu/source/i18n/unicode/"*.h "$SYS/include/unicode/"
    U="$WORK/gfopenbrowser"
    if [ "$(git -C "$U" rev-parse HEAD 2>/dev/null)" != "$UL_COMMIT" ]; then
        rm -rf "$U"; mkdir -p "$U"; git -C "$U" init -q
        git -C "$U" sparse-checkout set --no-cone /src/unicodelib/
        git -C "$U" fetch -q --depth 1 --filter=blob:none "$UL_REPO" "$UL_COMMIT"; git -C "$U" checkout -q FETCH_HEAD
    fi
    L="$U/src/unicodelib"; G="$WORK/obj/unicode"; rm -rf "$G"; mkdir -p "$G"
    python3 "$L/gen_calls.py" "$L/calls.txt" "$G" "$L/own.txt"
    m68k-amigaos-gcc -m68040 -c "$G/ul_stubs.s" -o "$G/ul_stubs.o"
    m68k-amigaos-gcc $CF -std=gnu99 -fno-common -I"$G" -I"$L" -I"$L/sdk/Include_H" -I"$SYS/include" -c "$L/ul_client.c" -o "$G/ul_client.o"
    rm -f "$SYS/lib/libicuuc.a"; m68k-amigaos-ar rcs "$SYS/lib/libicuuc.a" "$G/ul_stubs.o" "$G/ul_client.o"
    cp "$L/ul_client.h" "$SYS/include/"
    echo "$ICU_SHA256-$UL_COMMIT" > "$WORK/.stamp-unicode"
fi
echo "stage unicode: $(ls -l "$SYS/lib/libicuuc.a" | awk '{print $5}') bytes of stubs, $(ls "$SYS/include/unicode" | wc -l) headers"
