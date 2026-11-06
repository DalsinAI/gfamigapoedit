#pragma once
#include <string>
#include <vector>
#include <exception>
namespace dispatch { template<class T> struct future { T v; }; template<class T> future<T> make_ready_future(T v){return {v};}
  template<class T> future<T> make_exceptional_future_from_current(){ return {}; } }
struct Suggestion { Suggestion():score(0),localScore(0){} Suggestion(const std::wstring& t, double s, int l=0):text(t),score(s),localScore(l){}
  std::wstring text; double score; int localScore; std::string id;
  bool operator<(const Suggestion& o) const { return score > o.score; } };
typedef std::vector<Suggestion> SuggestionsList;
struct SuggestionQuery { Language srclang, lang; std::wstring source; };
struct SuggestionsBackend { virtual ~SuggestionsBackend(){} virtual dispatch::future<SuggestionsList> SuggestTranslation(const SuggestionQuery&& q)=0;
  virtual void Delete(const std::string&)=0; };
