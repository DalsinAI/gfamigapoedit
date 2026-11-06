#pragma once
#include "wx/string.h"
#include <sys/stat.h>
#define wxS_DIR_DEFAULT 0777
#define wxPATH_MKDIR_FULL 1
#define wxPATH_RMDIR_RECURSIVE 1
struct wxFileName { static bool Mkdir(const wxString& d,int,int){ mkdir(d.s.c_str(),0777); return true;} static bool Rmdir(const wxString& d,int){ system(("rm -rf '"+d.s+"'").c_str()); return true;} };
inline bool wxGetEnv(const char*n, wxString*o){ const char*v=getenv(n); if(!v) return false; *o=v; return true;}
inline wxString wxGetHomeDir(){ return getenv("HOME"); }
