#pragma once
#include <string>
#include <codecvt>
#include <locale>
namespace str { inline std::string to_utf8(const std::wstring& w){ std::wstring_convert<std::codecvt_utf8<wchar_t>> c; return c.to_bytes(w);} 
inline std::wstring to_wstring(const std::string& s){ std::wstring_convert<std::codecvt_utf8<wchar_t>> c; return c.from_bytes(s);}
inline std::wstring to_wstring(const std::wstring& s){ return s; } }
