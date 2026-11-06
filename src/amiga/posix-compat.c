/*
 * fchmod and fchown for Boost.Filesystem's copy_file, which the Amiga's C
 * library lacks. AmigaDOS keeps no Unix owner, and protection bits stay as
 * the file was made, so both succeed without changing anything. Here only
 * until GFOpenBoost carries them.
 * Copyright (c) 2026 AmiCradle Team
 */
#include <sys/types.h>

int fchmod(int fd, mode_t mode) { (void)fd; (void)mode; return 0; }
int fchown(int fd, uid_t owner, gid_t group) { (void)fd; (void)owner; (void)group; return 0; }
