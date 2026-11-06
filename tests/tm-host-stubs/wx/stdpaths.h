#pragma once
#include "wx/string.h"
struct wxStandardPaths { static wxStandardPaths& Get(){ static wxStandardPaths p; return p;} wxString GetUserDataDir(){ return getenv("TMDIR"); } };
