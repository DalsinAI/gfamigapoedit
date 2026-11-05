// Included first in every C++ file of this port (-include): the stove's
// pthread.h makes the POSIX cancellation points macros
// ("#define read(...) (pthread_testcancel(), read(__VA_ARGS__))"), which
// break every C++ member and function of those names (iostreams' read and
// close, chrono's read, the C++ library's messages::close). This takes
// pthread.h in once and drops them; pthread.h's include guard keeps them out
// after.
// Copyright (c) 2026 AmiCradle Team
#ifndef AMIGA_NOCANCEL_H
#define AMIGA_NOCANCEL_H
#include <unistd.h>
#include <pthread.h>
#undef accept
#undef close
#undef connect
#undef creat
#undef fcntl
#undef fsync
#undef lseek
#undef nanosleep
#undef open
#undef pause
#undef poll
#undef pread
#undef pwrite
#undef read
#undef readv
#undef recv
#undef recvfrom
#undef recvmsg
#undef select
#undef send
#undef sendmsg
#undef sendto
#undef sleep
#undef system
#undef tcdrain
#undef usleep
#undef wait
#undef waitpid
#undef write
#undef writev
#endif
