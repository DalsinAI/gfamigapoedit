#!/bin/sh
# Builds what Poedit needs besides wx into one sysroot: Boost's compiled
# parts (thread on pthreads, filesystem, chrono, date_time,
# iostreams without mmap/bzip2/lzma/zstd) and its headers, nlohmann json,
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
CXXF="-noixemul -m68040 -O2 -std=gnu++17 -fno-delete-null-pointer-checks -D_DEFAULT_SOURCE -include $HERE/scripts/amiga-nocancel.h"
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

# what the C++ library gives (Boost's config reads these)
printf "#include <string>\\n#include <atomic>\\n#include <mutex>\\n" | m68k-amigaos-g++ $CXXF -dM -E -x c++ - \
    | grep -E '_GLIBCXX_USE_WCHAR_T|_GLIBCXX_HAS_GTHREADS|_GLIBCXX_USE_C99|__GCC_ATOMIC_(CHAR|INT|POINTER)_LOCK_FREE|_REENTRANT|__amigaos__|__AMIGA__|_GLIBCXX_HAVE_WCHAR' | sort | sed 's/^/stage cxx: /'
grep -h 'O_DIRECTORY\|O_CLOEXEC\|define O_NOFOLLOW' "$TOOLCHAIN/m68k-amigaos/sys-include/fcntl.h" "$TOOLCHAIN/m68k-amigaos/sys-include/sys/"*fcntl*.h 2>/dev/null | sed 's/^/stage fcntl: /' || true
grep -c wstring "$TOOLCHAIN"/lib/gcc/m68k-amigaos/*/include/c++/bits/stringfwd.h | sed 's/^/stage wstring in stringfwd: /'

# which of the stove's headers define read, close or times as macros
for x in "" -U_DEFAULT_SOURCE; do
    printf '#include <iterator>\n' | m68k-amigaos-g++ $CXXF $x -dM -E -x c++ - | grep -E '#define (read|write|close|open|times|lseek|get|put)[ (]' | sed "s/^/stage macro$x: /" || true
done
printf '#include <iterator>\n' | m68k-amigaos-g++ $CXXF -H -E -x c++ - 2>&1 >/dev/null | grep -A12 'gthr-default' | head -16 | sed 's/^/stage pthread.h: /' || true

# Boost
if ! stamp boost "$BOOST_SHA256"; then
    fetch "$BOOST_URL" "$BOOST_SHA256" boost.tar.xz
    B="$WORK/boost-$BOOST_VERSION"; rm -rf "$B"
    tar -C "$WORK" -xJf "$D/boost.tar.xz"
    rm -rf "$SYS/include/boost"; cp -R "$B/boost" "$SYS/include/boost"
    cp "$HERE/scripts/boost/amigaos.hpp" "$SYS/include/boost/config/platform/amigaos.hpp"
    cp "$HERE/scripts/amiga-nocancel.h" "$SYS/include/amiga-nocancel.h"
    # the stove's pthreads have no pthread_condattr_setclock: condition
    # variables wait on the real-time clock, as Boost does where there is no
    # CLOCK_MONOTONIC
    sed -i 's/^  #if defined(CLOCK_MONOTONIC)$/  #if defined(CLOCK_MONOTONIC) \&\& !defined(__amigaos__)/' \
        "$SYS/include/boost/thread/detail/config.hpp"
    grep -q 'CLOCK_MONOTONIC) && !defined(__amigaos__)' "$SYS/include/boost/thread/detail/config.hpp" \
        || { echo "boost thread config: clock line not found"; exit 1; }
    O="$WORK/obj/boost"; rm -rf "$O"; mkdir -p "$O"
    DEF="-DBOOST_ALL_NO_LIB -DBOOST_THREAD_POSIX -DBOOST_THREAD_BUILD_LIB -DBOOST_THREAD_VERSION=4 \
         -DBOOST_FILESYSTEM_STATIC_LINK -DBOOST_FILESYSTEM_NO_CXX20_ATOMIC_REF -DO_DIRECTORY=0 -DBOOST_IOSTREAMS_NO_LIB"
    set --
    for f in thread/src/pthread/thread.cpp thread/src/pthread/once.cpp thread/src/future.cpp \
             filesystem/src/codecvt_error_category.cpp filesystem/src/directory.cpp filesystem/src/exception.cpp \
             filesystem/src/operations.cpp filesystem/src/path.cpp filesystem/src/path_traits.cpp \
             filesystem/src/portability.cpp filesystem/src/unique_path.cpp filesystem/src/utf8_codecvt_facet.cpp \
             chrono/src/chrono.cpp chrono/src/thread_clock.cpp chrono/src/process_cpu_clocks.cpp \
             date_time/src/gregorian/greg_month.cpp \
             iostreams/src/file_descriptor.cpp iostreams/src/zlib.cpp iostreams/src/gzip.cpp; do
        set -- "$@" "$f"
    done
    fails=0
    for f in "$@"; do
        o="$O/$(echo "$f" | tr / _ | sed 's/\.cpp$/.o/')"
        ( m68k-amigaos-g++ $CXXF $DEF -I"$SYS/include" -I"$PD/include" -I"$B/libs/filesystem/src" -I"$B/libs/atomic/src" \
            -c "$B/libs/$f" -o "$o" 2> "$o.err" || { echo "FAIL $f"; head -c 1500 "$o.err"; exit 1; } ) &
        while [ "$(jobs -p | wc -l)" -ge "$JOBS" ]; do sleep 1; done
    done
    wait || true
    for f in "$@"; do
        o="$O/$(echo "$f" | tr / _ | sed 's/\.cpp$/.o/')"
        [ -f "$o" ] || { fails=$((fails+1)); echo "FAIL $f"; head -c 2000 "$o.err"; }
    done
    grep -h "error" "$O"/*.err 2>/dev/null | sort | uniq -c | sort -rn | head -20
    if [ "$fails" -ne 0 ]; then
        first=$(ls "$O"/*.err | while read e; do [ -f "${e%.err}" ] || { echo "$e"; break; }; done)
        echo "first failure in full: $first"; head -40 "$first" | sed 's/^ *//; s/^/ERR /'
        m68k-amigaos-g++ $CXXF $DEF -I"$SYS/include" -I"$PD/include" -E -dD "$B/libs/chrono/src/process_cpu_clocks.cpp" 2>/dev/null \
            | grep -E '^# [0-9]+ "|^#define (read|close|times)\b' | grep -B1 -E '^#define (read|close|times)\b' | sed 's/^/ERR macro /' | head -12
        echo "boost: $fails files failed"; exit 1
    fi
    rm -f "$SYS/lib/libboost_poedit.a"; m68k-amigaos-ar rcs "$SYS/lib/libboost_poedit.a" "$O"/*.o
    echo "$BOOST_SHA256" > "$WORK/.stamp-boost"
fi
echo "stage boost: $(ls -l "$SYS/lib/libboost_poedit.a" | awk '{print $5}') bytes"

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
