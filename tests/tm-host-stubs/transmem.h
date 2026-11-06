#pragma once
#include <exception>
#include <functional>
#include <string>
#include <vector>
#include <memory>
#include "catalog.h"
#include "suggestions.h"
class TranslationMemoryImpl;
class TranslationMemory : public SuggestionsBackend {
public:
    static TranslationMemory& Get();
    static void CleanUp();
    SuggestionsList Search(const Language& srclang, const Language& lang, const std::wstring& source);
    dispatch::future<SuggestionsList> SuggestTranslation(const SuggestionQuery&& q) override;
    void Delete(const std::string& id) override;
    class IOInterface { public: virtual ~IOInterface() {}
        virtual void Insert(const Language&, const Language&, const std::wstring&, const std::wstring&, time_t) = 0; };
    void ExportData(IOInterface& destination);
    void ImportData(std::function<void(IOInterface&)> source);
    void SearchSubstring(IOInterface& destination, const Language& srclang, const Language& lang, const std::wstring& sourcePhrase);
    class Writer : public IOInterface { public: virtual ~Writer() {}
        void Insert(const Language&, const Language&, const std::wstring&, const std::wstring&, time_t) override = 0;
        virtual void Insert(const Language&, const Language&, const std::wstring&, const std::wstring&) = 0;
        virtual void Insert(const Language& srclang, const Language& lang, const CatalogItemPtr& item) = 0;
        virtual void Insert(const CatalogPtr& cat) = 0;
        virtual void Delete(const std::string& uuid) = 0;
        virtual void DeleteAll() = 0;
        virtual void Commit() = 0;
        virtual void Rollback() = 0; };
    std::shared_ptr<Writer> GetWriter();
    void DeleteAllAndReset();
    void GetStats(long& numDocs, long& fileSize);
private:
    TranslationMemory();
    ~TranslationMemory();
    TranslationMemoryImpl *m_impl;
    std::exception_ptr m_error;
    static TranslationMemory *ms_instance;
};
