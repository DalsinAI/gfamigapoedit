#pragma once
#include <cwctype>
typedef int UChar32;
enum { UCHAR_IDEOGRAPHIC = 1 };
enum { U_NON_SPACING_MARK = 6, U_COMBINING_SPACING_MARK = 8 };
inline bool u_hasBinaryProperty(UChar32 c, int){ return c>=0x4E00 && c<=0x9FFF; }
inline bool u_isalnum(UChar32 c){ return iswalnum(c); }
inline int u_charType(UChar32){ return 0; }
inline UChar32 u_tolower(UChar32 c){ return towlower(c); }
