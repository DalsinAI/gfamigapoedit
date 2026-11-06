#!/bin/sh
# ICU 78.3 for AmigaOS 3 (68040; soft float, as the rest of Poedit), the way
# GFOpenBrowser builds it (its scripts/build-icu.sh and AmigaOS patch, from
# the commit poedit.lock pins): a host ICU first for the data tools, then
# the cross build with static libraries and all the data linked in.
#   TOOLCHAIN=.../prefix WORK=build scripts/build-icu.sh
# Stamps in WORK let a capped run go on next time.
# Copyright (c) 2026 AmiCradle Team
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
. "$HERE/poedit.lock"
WORK=${WORK:-$HOME/.cache/gfamigapoedit/build}; JOBS=${JOBS:-12}
SYS="$WORK/sysroot"; D="$WORK/dl"; I="$WORK/icu"
P="$TOOLCHAIN"
mkdir -p "$D" "$I"
stamp() { [ "$(cat "$WORK/.stamp-$1" 2>/dev/null)" = "$2" ]; }
KEY="$ICU_SHA256-$OB_COMMIT"
stamp icu "$KEY" && { echo "stage icu: done before"; exit 0; }

if [ ! -f "$D/icu.tgz" ] || ! echo "$ICU_SHA256  $D/icu.tgz" | sha256sum -c - >/dev/null 2>&1; then
    curl -fsSL -o "$D/icu.tgz.part" "$ICU_URL"
    echo "$ICU_SHA256  $D/icu.tgz.part" | sha256sum -c - >/dev/null || { echo "sha256 mismatch: $ICU_URL"; exit 1; }
    mv "$D/icu.tgz.part" "$D/icu.tgz"
fi
OB="$I/gfopenbrowser"
if [ "$(git -C "$OB" rev-parse HEAD 2>/dev/null)" != "$OB_COMMIT" ]; then
    rm -rf "$OB"; mkdir -p "$OB"; git -C "$OB" init -q
    git -C "$OB" fetch -q --depth 1 "$OB_REPO" "$OB_COMMIT"; git -C "$OB" checkout -q FETCH_HEAD
fi

# sources: the host build uses them as they come, the cross build patched
if ! stamp icu-src "$KEY"; then
    rm -rf "$I/host-src" "$I/src"; mkdir -p "$I/host-src" "$I/src"
    tar -C "$I/host-src" -xzf "$D/icu.tgz"; tar -C "$I/src" -xzf "$D/icu.tgz"
    (cd "$I/src/icu" && patch -p1 -s < "$OB/icu/patches/0001-icu78-amigaos3.patch")
    echo "$KEY" > "$WORK/.stamp-icu-src"
fi

if ! stamp icu-host "$KEY"; then
    rm -rf "$I/host"; mkdir -p "$I/host"
    (cd "$I/host" && "$I/host-src/icu/source/configure" --disable-tests --disable-samples --disable-extras > configure.log 2>&1 \
        && make -j"$JOBS" > build.log 2>&1) || { tail -30 "$I/host/build.log" "$I/host/configure.log" 2>/dev/null; exit 1; }
    echo "$KEY" > "$WORK/.stamp-icu-host"
fi
echo "stage icu host: built"

F="-O2 -fno-delete-null-pointer-checks -m68040"
B="$I/cross"; rm -rf "$B"; mkdir -p "$B"; cd "$B"
CC=$P/bin/m68k-amigaos-gcc CXX=$P/bin/m68k-amigaos-g++ AR=$P/bin/m68k-amigaos-ar RANLIB=$P/bin/m68k-amigaos-ranlib \
CPPFLAGS="-DU_TIMEZONE=_timezone -DU_ENABLE_DYLOAD=0 -DHAVE_DLOPEN=0" CFLAGS="$F -noixemul" \
CXXFLAGS="$F -noixemul -include $HERE/scripts/amiga-nocancel.h" LDFLAGS="$F -noixemul" LIBS="-lpthread -lstdc++" \
"$I/src/icu/source/configure" --host=m68k-amigaos --build=$(gcc -dumpmachine) --with-cross-build="$I/host" \
  --enable-static --disable-shared --disable-tools --disable-tests --disable-samples --disable-extras \
  --disable-icuio --disable-layoutex --with-data-packaging=static --prefix="$SYS" > configure.log 2>&1 \
  || { tail -30 config.log; exit 1; }
# the libraries build first; data packaging then fails (pkgdata wants ELF objects)
make -j"$JOBS" > build.log 2>&1 || true
sed -i 's/^GENCCODE_ASSEMBLY_TYPE=$/GENCCODE_ASSEMBLY_TYPE=-a gcc-cygwin/' data/icupkg.inc
sed -i 's/^GENCCODE_ASSEMBLY = $/GENCCODE_ASSEMBLY = -a gcc-cygwin/' icudefs.mk
make -j"$JOBS" >> build.log 2>&1 || { grep -i 'error' build.log | head -30; exit 1; }
make install > install.log 2>&1 || { tail -20 install.log; exit 1; }
# genccode writes .long words in host order, which swaps them on m68k; take
# the big-endian .dat byte for byte instead (all of it: Poedit names every
# language)
DD="$I/data-incbin"; rm -rf "$DD"; mkdir -p "$DD"
cp data/out/icudt78b.dat "$DD/"
cat > "$DD/icudt78_dat.s" <<'S'
	.globl	_icudt78_dat
	.data
	.balign	8
_icudt78_dat:
	.incbin	"icudt78b.dat"
S
(cd "$DD" && "$P/bin/m68k-amigaos-as" -m68040 icudt78_dat.s -o icudt78_dat.o && rm -f libicudata.a && "$P/bin/m68k-amigaos-ar" rcs libicudata.a icudt78_dat.o)
cp "$DD/libicudata.a" "$SYS/lib/libicudata.a"
echo "$KEY" > "$WORK/.stamp-icu"
echo "stage icu: $(ls -l "$SYS/lib/libicuuc.a" "$SYS/lib/libicui18n.a" "$SYS/lib/libicudata.a" | awk '{print $9": "$5}' | tr '\n' ' ')"
