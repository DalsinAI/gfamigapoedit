// Boost's platform config for AmigaOS 3 with libnix, its newlib headers and
// the stove's pthreads. It takes the place of boost/config/platform/
// amigaos.hpp, which turns threads and std::wstring off; this C++ library
// has both (_GLIBCXX_HAS_GTHREADS, _GLIBCXX_USE_WCHAR_T).
// Copyright (c) 2026 AmiCradle Team
#define BOOST_PLATFORM "AmigaOS"
#define BOOST_HAS_UNISTD_H
#include <boost/config/detail/posix_features.hpp>
#ifndef BOOST_HAS_PTHREADS
#  define BOOST_HAS_PTHREADS
#endif
#define BOOST_HAS_GETTIMEOFDAY

// The stove's pthread.h makes the POSIX cancellation points macros
// ("#define read(...) (pthread_testcancel(), read(__VA_ARGS__))"), which
// break every C++ member and function of those names (iostreams' read and
// close, chrono's read, the C++ library's messages::close). Take pthread.h
// in here, once, and drop them; Boost's own thread interruption is not
// affected.
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
