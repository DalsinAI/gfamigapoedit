/*
 * fchown for SQLite's unix VFS (os_unix.c), which the Amiga's C library
 * lacks. AmigaDOS keeps no Unix owner, so it succeeds without changing
 * anything. fchmod is GFOpenBoost's real one (libboost_filesystem). Goes
 * when the translation memory moves to transmem.library.
 * Copyright (c) 2026 AmiCradle Team
 */
#include <sys/types.h>

int fchown(int fd, uid_t owner, gid_t group) { (void)fd; (void)owner; (void)group; return 0; }
