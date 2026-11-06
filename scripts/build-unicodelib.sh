#!/bin/sh
# unicode.library for the lab, built here from GFOpenBrowser at UL_COMMIT
# (the calls Poedit adds are not in a released copy yet): ICU 78.3 for the
# host and the 68040 with GFOpenBrowser's scripts/build-icu.sh, then its
# scripts/build-unicode-library.sh. Each stage leaves a stamp, so a run cut
# short goes on next run; nothing here is shipped with Poedit.
#   STOVE=... WORK=... OUT=... scripts/build-unicodelib.sh
# Copyright (c) 2026 AmiCradle Team
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
. "$HERE/poedit.lock"
U="$WORK/ul"; D="$WORK/dl"; JOBS=${UL_JOBS:-12}
mkdir -p "$U/shim" "$D"
t0=$(date +%s)
stamp() { [ "$(cat "$U/.stamp-$1" 2>/dev/null)" = "$2" ]; }
# gmake and unzip, where Daletop has neither
command -v gmake >/dev/null || ln -sf "$(command -v make)" "$U/shim/gmake"
command -v unzip >/dev/null || { printf '#!/bin/sh\nexec python3 -I -m zipfile -e "$2" .\n' > "$U/shim/unzip"; chmod +x "$U/shim/unzip"; }
PATH="$U/shim:$PATH"

# GFOpenBrowser's ICU patch, scripts and src/unicodelib; GFOpenSDK's NDK headers
B="$U/gfopenbrowser"
if [ "$(git -C "$B" rev-parse HEAD 2>/dev/null)" != "$UL_COMMIT" ]; then
    rm -rf "$B"; mkdir -p "$B"; git -C "$B" init -q
    git -C "$B" sparse-checkout set --no-cone /src/unicodelib/ /scripts/ /icu/
    git -C "$B" fetch -q --depth 1 --filter=blob:none "$UL_REPO" "$UL_COMMIT"; git -C "$B" checkout -q FETCH_HEAD
fi
N="$U/gfopensdk"
if [ "$(git -C "$N" rev-parse HEAD 2>/dev/null)" != "$UL_SDK_COMMIT" ]; then
    rm -rf "$N"; mkdir -p "$N"; git -C "$N" init -q
    git -C "$N" sparse-checkout set --no-cone /OpenNDK/Include_H/
    git -C "$N" fetch -q --depth 1 --filter=blob:none "$UL_SDK_REPO" "$UL_SDK_COMMIT"; git -C "$N" checkout -q FETCH_HEAD
fi

[ -f "$D/icu.tgz" ] && echo "$ICU_SHA256  $D/icu.tgz" | sha256sum -c - >/dev/null 2>&1 || {
    curl -fsSL -o "$D/icu.tgz.part" "$ICU_URL"
    echo "$ICU_SHA256  $D/icu.tgz.part" | sha256sum -c - >/dev/null; mv "$D/icu.tgz.part" "$D/icu.tgz"; }
P="$(sha256sum "$B/icu/patches/0001-icu78-amigaos3.patch" "$B/scripts/build-icu.sh" "$B/icu/data-keep.lst" | sha256sum | cut -c1-16)"

# ICU for the host (its tools and data build the cross one)
if ! stamp host "$ICU_SHA256"; then
    # unpacked and configured once; make goes on from its objects if a run is cut short
    if ! stamp src "$ICU_SHA256"; then
        rm -rf "$U/icu-src" "$U/icu-host"; mkdir -p "$U/icu-src" "$U/icu-host"
        tar -C "$U/icu-src" -xzf "$D/icu.tgz"
        patch -s -p1 -d "$U/icu-src/icu" < "$B/icu/patches/0001-icu78-amigaos3.patch"
        (cd "$U/icu-host" && ../icu-src/icu/source/configure --disable-tests --disable-samples > configure.log 2>&1) \
            || { tail -20 "$U/icu-host/configure.log"; exit 1; }
        echo "$ICU_SHA256" > "$U/.stamp-src"
    fi
    (cd "$U/icu-host" && make -j"$JOBS" > build.log 2>&1) || { grep -v warning "$U/icu-host/build.log" | tail -20 | cut -c1-300; exit 1; }
    echo "$ICU_SHA256" > "$U/.stamp-host"; rm -f "$U/.stamp-cross"
    echo "stage icu host: $(( $(date +%s) - t0 )) s"
fi
# ICU for the 68040 (build-icu.sh starts its folder afresh, so it needs a run of its own)
if ! stamp cross "$ICU_SHA256-$P"; then
    [ "$(( ${UL_DEADLINE:-0} - $(date +%s) ))" -gt 420 ] || [ "${UL_DEADLINE:-0}" = 0 ] || { echo "stage icu 68040: next run"; exit 3; }
    ICU_SRC="$U/icu-src/icu" ICU_HOST_BUILD="$U/icu-host" OS32_GCC16="$STOVE" OAB_DEPS="$U/deps" JOBS=$JOBS \
        sh "$B/scripts/build-icu.sh" > "$U/icu-cross.log" 2>&1 \
        || { tail -15 "$U/icu-cross.log"; tail -25 "$U/deps/icu-build/build.log" 2>/dev/null | grep -v warning | cut -c1-300; exit 1; }
    rm -rf "$U/deps/icu-build"
    echo "$ICU_SHA256-$P" > "$U/.stamp-cross"
    echo "stage icu 68040: $(( $(date +%s) - t0 )) s"
fi
if [ ! -f "$U/deps/ucd-17.0.0/security/IdentifierType.txt" ]; then
    rm -rf "$U/deps/ucd-17.0.0"; sh "$B/scripts/fetch-ucd.sh" "$U/deps/ucd-17.0.0" > "$U/ucd.log" 2>&1 || { tail -5 "$U/ucd.log"; exit 1; }
fi
# the library, its stubs and its self-tests
if ! stamp lib "$UL_COMMIT-$ICU_SHA256-$P-$UL_SDK_COMMIT"; then
    STOVE="$STOVE" OAB_DEPS="$U/deps" NDK_INCLUDE="$N/OpenNDK/Include_H" \
        sh "$B/scripts/build-unicode-library.sh" "$U/out" > "$U/lib.log" 2>&1 || { tail -25 "$U/lib.log" | cut -c1-300; exit 1; }
    echo "$UL_COMMIT-$ICU_SHA256-$P-$UL_SDK_COMMIT" > "$U/.stamp-lib"
fi
mkdir -p "$OUT/kit/Libs/Unicode" "$OUT/kit/Tests"
cp "$U/out/unicode.library" "$OUT/kit/Libs/"
cp "$U/out/Unicode/icudt78b.dat" "$OUT/kit/Libs/Unicode/"
cp "$U/out/tests/UnicodeTest" "$U/out/tests/UnicodeProps" "$OUT/kit/Tests/"
echo "stage unicode.library: $(wc -c < "$U/out/unicode.library") bytes, $(grep -v '^#' "$B/src/unicodelib/calls.txt" | grep -c .) calls"
