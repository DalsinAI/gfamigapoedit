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
CXXF="-noixemul -m68040 -O2 -std=gnu++17 -fno-delete-null-pointer-checks -D_DEFAULT_SOURCE"
PD="$TOOLCHAIN/m68k-amigaos/port-deps"

fetch() { # url sha file
    if [ ! -f "$D/$3" ] || ! echo "$2  $D/$3" | sha256sum -c - >/dev/null 2>&1; then
        curl -fsSL -o "$D/$3.part" "$1"
        echo "$2  $D/$3.part" | sha256sum -c - >/dev/null || { echo "sha256 mismatch: $1"; exit 1; }
        mv "$D/$3.part" "$D/$3"
    fi
}
stamp() { [ "$(cat "$WORK/.stamp-$1" 2>/dev/null)" = "$2" ]; }

# what the C++ library gives (Boost's config reads these)
printf "#include <string>\\n#include <atomic>\\n#include <mutex>\\n" | m68k-amigaos-g++ $CXXF -dM -E -x c++ - \
    | grep -E '_GLIBCXX_USE_WCHAR_T|_GLIBCXX_HAS_GTHREADS|_GLIBCXX_USE_C99|__GCC_ATOMIC_(CHAR|INT|POINTER)_LOCK_FREE|_REENTRANT|__amigaos__|__AMIGA__|_GLIBCXX_HAVE_WCHAR' | sort | sed 's/^/stage cxx: /'
grep -h 'O_DIRECTORY\|O_CLOEXEC\|define O_NOFOLLOW' "$TOOLCHAIN/m68k-amigaos/sys-include/fcntl.h" "$TOOLCHAIN/m68k-amigaos/sys-include/sys/"*fcntl*.h 2>/dev/null | sed 's/^/stage fcntl: /' || true
grep -c wstring "$TOOLCHAIN"/lib/gcc/m68k-amigaos/*/include/c++/bits/stringfwd.h | sed 's/^/stage wstring in stringfwd: /'

# which of the stove's headers define read, close or times as macros
printf '#include <pthread.h>\n' | m68k-amigaos-g++ $CXXF -dM -E -x c++ - | grep -E '#define (read|write|close|open|times|lseek|get|put)[ (]' | sed 's/^/stage macro: /' || true
printf '#include <pthread.h>\n' | m68k-amigaos-g++ $CXXF -H -E -x c++ - 2>&1 >/dev/null | grep '^\.' | head -30 | sed 's/^/stage pthread.h: /' || true

# Boost
if ! stamp boost "$BOOST_SHA256"; then
    fetch "$BOOST_URL" "$BOOST_SHA256" boost.tar.xz
    B="$WORK/boost-$BOOST_VERSION"; rm -rf "$B"
    tar -C "$WORK" -xJf "$D/boost.tar.xz"
    rm -rf "$SYS/include/boost"; cp -R "$B/boost" "$SYS/include/boost"
    cp "$HERE/scripts/boost/amigaos.hpp" "$SYS/include/boost/config/platform/amigaos.hpp"
    # the stove's pthreads have no pthread_condattr_setclock: condition
    # variables wait on the real-time clock, as Boost does where there is no
    # CLOCK_MONOTONIC
    sed -i 's/^  #if defined(CLOCK_MONOTONIC)$/  #if defined(CLOCK_MONOTONIC) \&\& !defined(__amigaos__)/' \
        "$SYS/include/boost/thread/detail/config.hpp"
    grep -q 'CLOCK_MONOTONIC) && !defined(__amigaos__)' "$SYS/include/boost/thread/detail/config.hpp" \
        || { echo "boost thread config: clock line not found"; exit 1; }
    O="$WORK/obj/boost"; rm -rf "$O"; mkdir -p "$O"
    DEF="-DBOOST_ALL_NO_LIB -DBOOST_THREAD_POSIX -DBOOST_THREAD_BUILD_LIB -DBOOST_THREAD_VERSION=4 \
         -DBOOST_FILESYSTEM_STATIC_LINK -DBOOST_FILESYSTEM_NO_CXX20_ATOMIC_REF -DBOOST_IOSTREAMS_NO_LIB"
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
        echo "boost: $fails files failed"; exit 1
    fi
    rm -f "$SYS/lib/libboost_poedit.a"; m68k-amigaos-ar rcs "$SYS/lib/libboost_poedit.a" "$O"/*.o
    echo "$BOOST_SHA256" > "$WORK/.stamp-boost"
fi
echo "stage boost: $(ls -l "$SYS/lib/libboost_poedit.a" | awk '{print $5}') bytes"
