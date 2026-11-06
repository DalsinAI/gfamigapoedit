#pragma once
#include <string>
#include <memory>
#include <vector>
#include <codecvt>
#include <locale>
struct Language { std::string c; Language(){} Language(std::string s):c(s){}
  bool IsValid() const { return !c.empty(); } const std::string& Code() const { return c; }
  std::wstring WCode() const { return std::wstring(c.begin(), c.end()); }
  std::string Lang() const { return c.substr(0, c.find('_')); }
  unsigned nplurals() const { return 2; }
  bool operator==(const Language& o) const { return c==o.c; }
  static Language TryParse(const std::string& s){ return Language(s);} };
struct CatalogItem { bool HasError() const {return false;} bool IsFuzzy() const {return false;} bool IsPreTranslated() const {return false;}
  bool IsTranslated() const {return true;} bool HasPlural() const {return false;}
  std::wstring s,t; std::wstring GetString() const {return s;} std::wstring GetPluralString() const {return s;}
  std::wstring GetTranslation(int=0) const {return t;} };
typedef std::shared_ptr<CatalogItem> CatalogItemPtr;
struct Catalog { std::vector<CatalogItemPtr> v; std::vector<CatalogItemPtr>& items(){return v;}
  Language GetSourceLanguage(){return Language("en");} Language GetLanguage(){return Language("cs");} };
typedef std::shared_ptr<Catalog> CatalogPtr;
