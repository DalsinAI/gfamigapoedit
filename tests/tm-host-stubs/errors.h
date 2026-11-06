#pragma once
#include <stdexcept>
#include "wx/string.h"
struct Exception : std::runtime_error { Exception(const wxString& s):std::runtime_error(s.s){} };
#define BOOST_THROW_EXCEPTION(x) throw x
