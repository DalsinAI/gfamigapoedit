#pragma once
#include <string>
#include <algorithm>
#include <cwctype>
namespace boost { namespace algorithm { inline bool ifind_first(const std::wstring& h, const std::wstring& n){
 auto it = std::search(h.begin(), h.end(), n.begin(), n.end(), [](wchar_t a, wchar_t b){ return towlower(a)==towlower(b); }); return it!=h.end(); } } }
