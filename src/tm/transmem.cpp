/*
 *  Poedit's translation memory on transmem.library (GFOpenTransMem), in
 *  place of its Lucene++ one. The library keeps Poedit's behaviour: exact
 *  phrase first, then a phrase with one word of slack, then a search by
 *  words, with Poedit's scores, thresholds, merging and entry ids.
 *
 *  One database file, TranslationMemory/tm.sqlite under Poedit's data
 *  folder (the library's own format, on gfopensqlite).
 *
 *  Copyright (c) 2026 AmiCradle Team
 */

#include "transmem.h"

#include "catalog.h"
#include "errors.h"
#include "progress.h"
#include "str_helpers.h"
#include "utility.h"

#include <wx/stdpaths.h>
#include <wx/utils.h>
#include <wx/filename.h>
#include <wx/translation.h>

#include <mutex>
#include <time.h>

#include <proto/transmem.h>

namespace
{

#define REG(r, decl) register decl __asm(#r)

[[noreturn]] void ThrowTM(struct TMDatabase *db, LONG err)
{
    const wxString msg = TransMemBase ? wxString::FromUTF8((const char*)TM_ErrorText(db, err))
                                      : wxString("transmem.library is not installed");
    if (err == TMERR_CORRUPT || err == TMERR_CANTOPEN)
        BOOST_THROW_EXCEPTION(Exception(wxString::Format(_("Translation memory database is corrupted: %s (%d)."),
                                                         msg, (int)err)));
    BOOST_THROW_EXCEPTION(Exception(wxString::Format(_("Translation memory error: %s (%d)."),
                                                     msg, (int)err)));
}

void Check(struct TMDatabase *db, LONG err)
{
    if (err != TMERR_OK)
        ThrowTM(db, err);
}

// frees a result block when it goes out of scope
struct ResultsHolder
{
    struct TMResults *r = nullptr;
    ~ResultsHolder() { if (r) TM_FreeResults(r); }
};

struct ExportState
{
    TranslationMemory::IOInterface *dest;
    Progress *progress;
};

ULONG ExportOne(REG(a0, struct Hook *h), REG(a2, struct TMDatabase * /*db*/), REG(a1, struct TMEntry *e))
{
    auto st = (ExportState*)h->h_Data;
    st->progress->increment();
    st->dest->Insert(Language::TryParse((const char*)e->tme_SourceLang),
                     Language::TryParse((const char*)e->tme_Lang),
                     str::to_wstring((const char*)e->tme_Source),
                     str::to_wstring((const char*)e->tme_Translation),
                     (time_t)e->tme_Created);
    return 0;
}

} // anonymous namespace


// ----------------------------------------------------------------
// TranslationMemoryImpl
// ----------------------------------------------------------------

class TranslationMemoryImpl;
std::shared_ptr<TranslationMemory::Writer> MakeWriter(TranslationMemoryImpl& tm);

class TranslationMemoryImpl
{
public:
    TranslationMemoryImpl() : m_db(nullptr) { Init(); }
    ~TranslationMemoryImpl();

    SuggestionsList Search(const Language& srclang, const Language& lang,
                           const std::wstring& source);

    void ExportData(TranslationMemory::IOInterface& destination);
    void ImportData(std::function<void(TranslationMemory::IOInterface&)> source);

    void SearchSubstring(TranslationMemory::IOInterface& destination,
                         const Language& srclang, const Language& lang, const std::wstring& sourcePhrase);

    std::shared_ptr<TranslationMemory::Writer> GetWriter() { return m_writerAPI; }

    void GetStats(long& numDocs, long& fileSize);

    static std::wstring GetDatabaseDir();

    // used by the writer
    void Insert(const Language& srclang, const Language& lang,
                const std::wstring& source, const std::wstring& trans, time_t creationTime);
    void Delete(const std::string& uuid);
    void DeleteAll();
    void Commit();
    void Rollback();

private:
    void Init();

    struct TMDatabase *m_db;
    std::recursive_mutex m_mutex;
    std::shared_ptr<TranslationMemory::Writer> m_writerAPI;
};


std::wstring TranslationMemoryImpl::GetDatabaseDir()
{
    wxString data = wxStandardPaths::Get().GetUserDataDir();

    // ensure the parent directory exists:
    wxFileName::Mkdir(data, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);

    data += wxFILE_SEP_PATH;
    data += "TranslationMemory";

    return data.ToStdWstring();
}


void TranslationMemoryImpl::Init()
{
    if (!TransMemBase)
        ThrowTM(nullptr, TMERR_CANTOPEN);

    const wxString dir(GetDatabaseDir());
    wxFileName::Mkdir(dir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
    const wxString path = dir + wxFILE_SEP_PATH + "tm.sqlite";

    LONG err = TMERR_OK;
    m_db = TM_OpenDatabase((CONST_STRPTR)(const char*)path.fn_str(), &err);
    if (!m_db)
        ThrowTM(nullptr, err);

    m_writerAPI = MakeWriter(*this);
}


TranslationMemoryImpl::~TranslationMemoryImpl()
{
    // uncommitted changes are committed on close, as the Writer promises
    // (and as TM_CloseDatabase does)
    m_writerAPI.reset();
    if (m_db)
        TM_CloseDatabase(m_db);
}


void TranslationMemoryImpl::Commit()
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    Check(m_db, TM_Commit(m_db));
}


void TranslationMemoryImpl::Rollback()
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    Check(m_db, TM_Rollback(m_db));
}


void TranslationMemoryImpl::Insert(const Language& srclang, const Language& lang,
                                   const std::wstring& source, const std::wstring& trans,
                                   time_t creationTime)
{
    if (!lang.IsValid() || !srclang.IsValid() || lang == srclang)
        return;

    const std::string sl = srclang.Code(), l = lang.Code();
    const std::string src8 = str::to_utf8(source), trans8 = str::to_utf8(trans);

    struct TMEntry e = {};
    e.tme_SourceLang = (CONST_STRPTR)sl.c_str();
    e.tme_Lang = (CONST_STRPTR)l.c_str();
    e.tme_Source = (CONST_STRPTR)src8.c_str();
    e.tme_Translation = (CONST_STRPTR)trans8.c_str();
    e.tme_Created = (LONG)creationTime;   // 0 is now

    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    Check(m_db, TM_Insert(m_db, &e));
}


void TranslationMemoryImpl::Delete(const std::string& uuid)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    Check(m_db, TM_Delete(m_db, (CONST_STRPTR)uuid.c_str()));
}


void TranslationMemoryImpl::DeleteAll()
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    Check(m_db, TM_DeleteAll(m_db));
}


SuggestionsList TranslationMemoryImpl::Search(const Language& srclang,
                                              const Language& lang,
                                              const std::wstring& source)
{
    try
    {
        const std::string sl = srclang.Code(), l = lang.Code();
        const std::string text = str::to_utf8(source);
        struct TMQuery q = {};
        q.tmq_SourceLang = (CONST_STRPTR)sl.c_str();
        q.tmq_Lang = (CONST_STRPTR)l.c_str();
        q.tmq_Text = (CONST_STRPTR)text.c_str();
        q.tmq_Max = 0;   // Poedit's 10

        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        ResultsHolder res;
        Check(m_db, TM_Search(m_db, &q, &res.r));

        SuggestionsList results;
        for (ULONG i = 0; res.r && i < res.r->tmrs_Count; i++)
        {
            const auto& hit = res.r->tmrs_Results[i];
            Suggestion r {str::to_wstring((const char*)hit.tmr_Entry.tme_Translation),
                          hit.tmr_Score / double(TM_SCORE_ONE),
                          int(hit.tmr_Entry.tme_Created)};
            if (hit.tmr_Entry.tme_Id)
                r.id = (const char*)hit.tmr_Entry.tme_Id;
            results.push_back(std::move(r));
        }
        return results;
    }
    catch (...)
    {
        return SuggestionsList();
    }
}


void TranslationMemoryImpl::SearchSubstring(TranslationMemory::IOInterface& destination,
                                            const Language& srclang, const Language& lang,
                                            const std::wstring& sourcePhrase)
{
    const std::string sl = srclang.Code(), l = lang.Code();
    const std::string text = str::to_utf8(sourcePhrase);
    struct TMQuery q = {};
    q.tmq_SourceLang = (CONST_STRPTR)sl.c_str();
    q.tmq_Lang = (CONST_STRPTR)l.c_str();
    q.tmq_Text = (CONST_STRPTR)text.c_str();
    q.tmq_Max = 0;   // all

    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    ResultsHolder res;
    Check(m_db, TM_SearchSubstring(m_db, &q, &res.r));
    for (ULONG i = 0; res.r && i < res.r->tmrs_Count; i++)
    {
        const auto& e = res.r->tmrs_Results[i].tmr_Entry;
        destination.Insert(srclang, lang,
                           str::to_wstring((const char*)e.tme_Source),
                           str::to_wstring((const char*)e.tme_Translation),
                           (time_t)e.tme_Created);
    }
}


void TranslationMemoryImpl::ExportData(TranslationMemory::IOInterface& destination)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    struct TMStats stats = {};
    Check(m_db, TM_GetStats(m_db, &stats));
    Progress progress((int)stats.tms_Entries);

    ExportState st { &destination, &progress };
    struct Hook h = {};
    h.h_Entry = (HOOKFUNC)(APTR)ExportOne;
    h.h_Data = &st;
    Check(m_db, TM_ForEach(m_db, &h));
}


void TranslationMemoryImpl::ImportData(std::function<void(TranslationMemory::IOInterface&)> source)
{
    auto writer = TranslationMemory::Get().GetWriter();
    source(*writer);
    writer->Commit();
}


void TranslationMemoryImpl::GetStats(long& numDocs, long& fileSize)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    struct TMStats stats = {};
    Check(m_db, TM_GetStats(m_db, &stats));
    numDocs = (long)stats.tms_Entries;
    fileSize = (long)stats.tms_FileSize;
}


// ----------------------------------------------------------------
// TranslationMemoryWriterImpl
// ----------------------------------------------------------------

class TranslationMemoryWriterImpl : public TranslationMemory::Writer
{
public:
    explicit TranslationMemoryWriterImpl(TranslationMemoryImpl& tm) : m_tm(tm) {}

    void Commit() override { m_tm.Commit(); }
    void Rollback() override { m_tm.Rollback(); }

    void Insert(const Language& srclang, const Language& lang,
                const std::wstring& source, const std::wstring& trans,
                time_t creationTime) override
    {
        m_tm.Insert(srclang, lang, source, trans, creationTime);
    }

    void Insert(const Language& srclang, const Language& lang,
                const std::wstring& source, const std::wstring& trans) override
    {
        Insert(srclang, lang, source, trans, 0);
    }

    void Insert(const Language& srclang, const Language& lang, const CatalogItemPtr& item) override
    {
        if (!lang.IsValid() || !srclang.IsValid())
            return;

        // translations with errors, untranslated, pre-translated, fuzzy: not kept
        if (item->HasError())
            return;
        if (item->IsFuzzy() || item->IsPreTranslated() || !item->IsTranslated())
            return;

        // the singular always
        Insert(srclang, lang, str::to_wstring(item->GetString()), str::to_wstring(item->GetTranslation()));

        // plurals where the language has one or two forms
        if (item->HasPlural())
        {
            switch (lang.nplurals())
            {
                case 1:
                    Insert(srclang, lang, str::to_wstring(item->GetPluralString()), str::to_wstring(item->GetTranslation()));
                    break;
                case 2:
                    Insert(srclang, lang, str::to_wstring(item->GetPluralString()), str::to_wstring(item->GetTranslation(1)));
                    break;
                default:
                    break;
            }
        }
    }

    void Insert(const CatalogPtr& cat) override
    {
        Progress progress((int)cat->items().size());

        auto srclang = cat->GetSourceLanguage();
        auto lang = cat->GetLanguage();
        if (!lang.IsValid() || !srclang.IsValid())
            return;

        for (auto& item: cat->items())
        {
            Insert(srclang, lang, item);
            progress.increment();
        }
    }

    void Delete(const std::string& uuid) override { m_tm.Delete(uuid); }
    void DeleteAll() override { m_tm.DeleteAll(); }

private:
    TranslationMemoryImpl& m_tm;
};

std::shared_ptr<TranslationMemory::Writer> MakeWriter(TranslationMemoryImpl& tm)
{
    return std::make_shared<TranslationMemoryWriterImpl>(tm);
}


// ----------------------------------------------------------------
// Singleton management
// ----------------------------------------------------------------

static std::once_flag initializationFlag;
TranslationMemory *TranslationMemory::ms_instance = nullptr;

TranslationMemory& TranslationMemory::Get()
{
    std::call_once(initializationFlag, []() {
        ms_instance = new TranslationMemory;
    });
    return *ms_instance;
}

void TranslationMemory::CleanUp()
{
    if (ms_instance)
    {
        delete ms_instance;
        ms_instance = nullptr;
    }
}

TranslationMemory::TranslationMemory() : m_impl(nullptr)
{
    try
    {
        m_impl = new TranslationMemoryImpl;
    }
    catch (...)
    {
        m_error = std::current_exception();
    }
}

TranslationMemory::~TranslationMemory() { delete m_impl; }


// ----------------------------------------------------------------
// public API
// ----------------------------------------------------------------

SuggestionsList TranslationMemory::Search(const Language& srclang,
                                          const Language& lang,
                                          const std::wstring& source)
{
    if (!m_impl)
        std::rethrow_exception(m_error);
    return m_impl->Search(srclang, lang, source);
}

dispatch::future<SuggestionsList> TranslationMemory::SuggestTranslation(const SuggestionQuery&& q)
{
    try
    {
        return dispatch::make_ready_future(Search(q.srclang, q.lang, q.source));
    }
    catch (...)
    {
        return dispatch::make_exceptional_future_from_current<SuggestionsList>();
    }
}

void TranslationMemory::Delete(const std::string& id)
{
    auto tm = TranslationMemory::Get().GetWriter();
    tm->Delete(id);
    tm->Commit();
}

void TranslationMemory::ExportData(IOInterface& destination)
{
    if (!m_impl)
        std::rethrow_exception(m_error);
    return m_impl->ExportData(destination);
}

void TranslationMemory::ImportData(std::function<void(IOInterface&)> source)
{
    if (!m_impl)
        std::rethrow_exception(m_error);
    return m_impl->ImportData(source);
}

std::shared_ptr<TranslationMemory::Writer> TranslationMemory::GetWriter()
{
    if (!m_impl)
        std::rethrow_exception(m_error);
    return m_impl->GetWriter();
}

void TranslationMemory::DeleteAllAndReset()
{
    try
    {
        auto tm = TranslationMemory::Get().GetWriter();
        tm->DeleteAll();
        tm->Commit();
    }
    catch (...)
    {
        // the database is damaged: delete it and start a new one
        delete m_impl;
        m_impl = nullptr;
        wxFileName::Rmdir(TranslationMemoryImpl::GetDatabaseDir(), wxPATH_RMDIR_RECURSIVE);
        m_impl = new TranslationMemoryImpl;
        m_error = nullptr;
    }
}

void TranslationMemory::GetStats(long& numDocs, long& fileSize)
{
    if (!m_impl)
        std::rethrow_exception(m_error);
    m_impl->GetStats(numDocs, fileSize);
}

void TranslationMemory::SearchSubstring(IOInterface& destination,
                                        const Language& srclang, const Language& lang, const std::wstring& sourcePhrase)
{
    if (!m_impl)
        std::rethrow_exception(m_error);
    m_impl->SearchSubstring(destination, srclang, lang, sourcePhrase);
}
