#include "transmem.h"
#include <cstdio>
#include <clocale>
static void show(const char* q, const SuggestionsList& r){ printf("Q: %s\n", q); for (auto& s: r) printf("   %.3f  %ls  [%s]\n", s.score, s.text.c_str(), s.id.c_str()); }
int main(){
  setlocale(LC_ALL, "C.UTF-8");
  auto& tm = TranslationMemory::Get();
  auto w = tm.GetWriter();
  Language en("en"), cs("cs"), csCZ("cs_CZ"), de("de");
  w->Insert(en, cs, L"Open File", L"Otevřít soubor");
  w->Insert(en, cs, L"Open file", L"Otevřít soubor");
  w->Insert(en, cs, L"Open the file now", L"Otevřít soubor teď");
  w->Insert(en, cs, L"Save File", L"Uložit soubor");
  w->Insert(en, csCZ, L"Close File", L"Zavřít soubor");
  w->Insert(en, de, L"Open File", L"Datei öffnen");
  w->Insert(en, cs, L"Don't save changes to the document", L"Neukládat změny dokumentu");
  w->Insert(en, cs, L"An error occurred while saving the file to disk", L"Při ukládání souboru došlo k chybě");
  w->Commit();
  show("Open File", tm.Search(en, cs, L"Open File"));
  show("open files", tm.Search(en, cs, L"open files"));
  show("Close File (cs)", tm.Search(en, cs, L"Close File"));
  show("Open file now", tm.Search(en, cs, L"Open file now"));
  show("Don't save changes to document", tm.Search(en, cs, L"Don't save changes to document"));
  show("An error occurred while saving the file", tm.Search(en, cs, L"An error occurred while saving the file"));
  show("Open File (de)", tm.Search(en, de, L"Open File"));
  w->Insert(en, cs, L"Temp", L"Dočasné"); w->Rollback();
  show("Temp after rollback", tm.Search(en, cs, L"Temp"));
  struct P : TranslationMemory::IOInterface { int n=0; void Insert(const Language& a,const Language& b,const std::wstring& s,const std::wstring& t,time_t){ n++; printf("   sub: %s %s %ls -> %ls\n", a.Code().c_str(), b.Code().c_str(), s.c_str(), t.c_str()); } } p;
  tm.SearchSubstring(p, en, cs, L"file");
  long n, sz; tm.GetStats(n, sz); printf("docs %ld\n", n);
  auto r = tm.Search(en, cs, L"Open File"); tm.Delete(r[0].id);
  show("Open File after delete", tm.Search(en, cs, L"Open File"));
  struct E : TranslationMemory::IOInterface { int n=0; void Insert(const Language&,const Language&,const std::wstring&,const std::wstring&,time_t){ n++; } } e;
  tm.ExportData(e); printf("export %d\n", e.n);
  tm.DeleteAllAndReset(); tm.GetStats(n, sz); printf("after reset %ld\n", n);
  TranslationMemory::CleanUp();
}
