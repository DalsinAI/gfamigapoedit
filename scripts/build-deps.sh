#!/bin/sh
# Builds what Poedit needs besides wx into one sysroot: Boost's compiled
# parts (thread on pthreads, filesystem, chrono, date_time,
# iostreams without mmap/bzip2/lzma/zstd) and its headers.
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
# cc_many OBJDIR SRCROOT "FLAGS" FILE...: compiles in parallel; on failure
# shows the first failing file's errors in full and stops
cc_many() {
    od=$1; root=$2; fl=$3; shift 3
    mkdir -p "$od"
    for f in "$@"; do
        o="$od/$(echo "$f" | tr / _ | sed 's/\.c[p]*$/.o/')"
        [ -f "$o" ] && [ "$o" -nt "$root/$f" ] && continue
        case "$f" in *.c) c=m68k-amigaos-gcc; std="-std=gnu99"; inc="" ;; *) c=m68k-amigaos-g++; std=""; inc="-include $HERE/scripts/amiga-nocancel.h" ;; esac
        ( $c $CF $std $inc $fl -c "$root/$f" -o "$o.part" 2> "$o.err" && mv "$o.part" "$o" ) &
        while [ "$(jobs -p | wc -l)" -ge "$JOBS" ]; do sleep 1; done
    done
    wait
    n=0
    for f in "$@"; do
        o="$od/$(echo "$f" | tr / _ | sed 's/\.c[p]*$/.o/')"
        if [ ! -f "$o" ]; then
            n=$((n+1))
            [ "$n" -gt 1 ] || { echo "FAIL $f"; head -40 "$o.err" | sed 's/^ *//; s/^/ERR /'; }
            echo "FAIL $f"
        fi
    done
    [ "$n" -eq 0 ] || { echo "$n files failed in $od"; exit 1; }
}

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

# pugixml and nlohmann json (Poedit's own pins)
if ! stamp pugixml "$PUGIXML_COMMIT"; then
    gitpin "$PUGIXML_GIT" "$PUGIXML_COMMIT" "$WORK/pugixml"
    cc_many "$WORK/obj/pugixml" "$WORK/pugixml" "-std=gnu++17" src/pugixml.cpp
    cp "$WORK/pugixml/src/pugixml.hpp" "$WORK/pugixml/src/pugiconfig.hpp" "$SYS/include/"
    rm -f "$SYS/lib/libpugixml.a"; m68k-amigaos-ar rcs "$SYS/lib/libpugixml.a" "$WORK/obj/pugixml"/*.o
    echo "$PUGIXML_COMMIT" > "$WORK/.stamp-pugixml"
fi
echo "stage pugixml: $(ls -l "$SYS/lib/libpugixml.a" | awk '{print $5}') bytes"
if ! stamp json "$JSON_COMMIT"; then
    gitpin "$JSON_GIT" "$JSON_COMMIT" "$WORK/json"
    rm -rf "$SYS/include/nlohmann"; cp -R "$WORK/json/single_include/nlohmann" "$SYS/include/nlohmann"
    echo "$JSON_COMMIT" > "$WORK/.stamp-json"
fi
echo "stage json: headers"

# Lucene++ (Poedit's fork; translation memory), built from the source list
# and Config.h Poedit's own build uses (deps/lucene)
if ! stamp lucene "$LUCENE_COMMIT"; then
    gitpin "$LUCENE_GIT" "$LUCENE_COMMIT" "$WORK/lucene"
    L="$WORK/lucene"; PL="$POE/deps/lucene"
    srcs=$(sed -n '/^  sources {/,/^  }/p' "$PL/LucenePlusPlus.bkl" | grep -E '^ +src/.*\.(cpp|c)$' | grep -v msvc | sed 's/^ *//')
    [ -n "$srcs" ] || { echo "no Lucene++ sources in $PL/LucenePlusPlus.bkl"; exit 1; }
    cc_many "$WORK/obj/lucene" "$L" "-std=gnu++17 -DLPP_BUILDING_LIB -I$PL -I$PL/std_replacements -I$L/include -I$L/src/core/include -I$SYS/include -I$PD/include" $srcs
    rm -f "$SYS/lib/liblucene++.a"; m68k-amigaos-ar rcs "$SYS/lib/liblucene++.a" "$WORK/obj/lucene"/*.o
    rm -rf "$SYS/include/lucene++"; mkdir -p "$SYS/include/lucene++"
    cp "$L"/include/*.h "$PL/Config.h" "$SYS/include/lucene++/"
    echo "$LUCENE_COMMIT" > "$WORK/.stamp-lucene"
fi
echo "stage lucene: $(ls -l "$SYS/lib/liblucene++.a" | awk '{print $5}') bytes"
