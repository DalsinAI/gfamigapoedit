#pragma once
#include <string>
#include <cstdio>
struct wxString { std::string s; wxString(){} wxString(const char*x):s(x){} wxString(const std::string&x):s(x){} wxString(const std::wstring&w):s(w.begin(),w.end()){}
  static wxString FromUTF8(const char*x){return wxString(x);} 
  template<class...A> static wxString Format(const wxString& f, A... a){ char b[512]; snprintf(b,512,f.s.c_str(), a...); return wxString(b);} 
  wxString& operator+=(const wxString&o){s+=o.s;return *this;} wxString& operator+=(char c){s+=c;return *this;}
  std::wstring ToStdWstring() const { return std::wstring(s.begin(), s.end()); }
  const char* utf8_str() const { return s.c_str(); } operator const char*() const { return s.c_str(); } };
inline wxString operator+(const wxString&a,const wxString&b){ return wxString(a.s+b.s);} 
inline wxString operator+(const wxString&a,const char*b){ return wxString(a.s+b);} 
inline wxString operator+(const wxString&a,char b){ return wxString(a.s+b);} 
#define _(x) wxString(x)
#define wxFILE_SEP_PATH '/'
