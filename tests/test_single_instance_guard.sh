#!/bin/sh
# Poedit's single-instance code needs both IPC and wxSingleInstanceChecker in
# the toolkit. Toolkit main has IPC but not the checker, so every guard in the
# patches must test both, and 0007's context lines must match 0003's guard.
HERE=$(cd "$(dirname "$0")/.." && pwd)
P3="$HERE/patches/0003-amiga-no-ipc-no-fswatcher.patch"
P7="$HERE/patches/0007-amiga-arexx-port.patch"
G='#if !defined(__WXOSX__) && wxUSE_IPC && wxUSE_SNGLINST_CHECKER'
fail=0
ok() { echo "ok - $1"; }
no() { echo "not ok - $1"; fail=1; }
[ "$(grep -cF "+$G" "$P3")" = 6 ] && ok "0003: all six guards test both" || no "0003: all six guards test both"
grep -q '^+#if !defined(__WXOSX__) && wxUSE_IPC$' "$P3" && no "0003: no guard on IPC alone" || ok "0003: no guard on IPC alone"
[ "$(grep -cF " $G" "$P7")" = 2 ] && ok "0007: context matches 0003" || no "0007: context matches 0003"
grep -q '^ #if !defined(__WXOSX__) && wxUSE_IPC$' "$P7" && no "0007: no stale context" || ok "0007: no stale context"
# the checker is made only inside the guard
awk -v g="+$G" '$0==g{f=1;next} f&&/new wxSingleInstanceChecker/{print "y";exit} f&&/^\+#endif/{f=0}' "$P3" | grep -q y \
  && ok "checker made inside the guard" || no "checker made inside the guard"
exit $fail
