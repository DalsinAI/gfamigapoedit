#!/bin/sh
# Sourced by the build scripts (needs HERE, CF, JOBS).
# Copyright (c) 2026 AmiCradle Team
# cc_many OBJDIR SRCROOT "FLAGS" FILE...: compiles in parallel, each
# compiler capped at CC_MEM_KB of memory (a runaway one fails on its own
# instead of taking Daletop's job runner with it); on failure
# shows the first failing file's errors in full and stops
cc_many() {
    od=$1; root=$2; fl=$3; shift 3; late=0
    mkdir -p "$od"
    for f in "$@"; do
        o="$od/$(echo "$f" | tr / _ | sed 's/\.c[p]*$/.o/')"
        [ -f "$o" ] && [ "$o" -nt "$root/$f" ] && continue
        # past the deadline: the rest next run (the objects made so far stay)
        if [ -n "${CC_DEADLINE:-}" ] && [ "$(date +%s)" -ge "$CC_DEADLINE" ]; then late=1; break; fi
        case "$f" in *.c) c=m68k-amigaos-gcc; std="-std=gnu99"; inc="" ;; *) c=m68k-amigaos-g++; std=""; inc="-include $HERE/scripts/amiga-nocancel.h" ;; esac
        ( ulimit -v ${CC_MEM_KB:-2500000}; $c $CF $std $inc $fl -c "$root/$f" -o "$o.part" 2> "$o.err" && mv "$o.part" "$o" ) &
        while [ "$(jobs -p | wc -l)" -ge "$JOBS" ]; do sleep 1; done
    done
    wait
    [ "$late" = 0 ] || { echo "stage time: $od goes on next run"; exit 3; }
    n=0
    for f in "$@"; do
        o="$od/$(echo "$f" | tr / _ | sed 's/\.c[p]*$/.o/')"
        if [ ! -f "$o" ]; then
            n=$((n+1))
            if [ "$n" -eq 1 ]; then echo "FAIL $f"; head -40 "$o.err" | sed 's/^ *//; s/^/ERR /'
            # the first errors of a few more files, so one run shows more
            elif [ "$n" -le 10 ]; then grep -m 4 'error' "$o.err" | cut -c1-300 | sed 's/^ *//; s/^/ERR /'; fi
            echo "FAIL $f"
        fi
    done
    [ "$n" -eq 0 ] || { echo "$n files failed in $od"; exit 1; }
}

