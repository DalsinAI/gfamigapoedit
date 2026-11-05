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

// pthread.h's cancellation-point macros out (see amiga-nocancel.h)
#include <amiga-nocancel.h>
