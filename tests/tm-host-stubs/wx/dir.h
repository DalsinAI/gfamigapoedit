#pragma once
#include "wx/string.h"
struct wxULL { long long v; long long GetValue() const {return v;} };
struct wxDir { static wxULL GetTotalSize(const wxString&){ return {0}; } };
