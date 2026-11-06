/*
 *  Poedit's translation memory on SQLite, in place of its Lucene++ one.
 *  Same class and same behaviour as Poedit's tm/transmem.h describes:
 *  exact phrase first, then a phrase with one word of slack, then a search
 *  by words; the same scores, thresholds and merging of hits.
 *
 *  One database file, TranslationMemory/tm.sqlite under Poedit's data
 *  folder:
 *    entries(id, uuid, srclang, lang, source, trans, created, ntok)
 *    terms(term, entry, pos)   the words of each source text
 *  Words are found as Lucene's StandardAnalyzer finds them: runs of letters
 *  and digits, lower case, every CJK ideograph or kana a word of its own,
 *  English stop words left out but counted in the positions.
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
#include <wx/dir.h>
#include <wx/filename.h>
#include <wx/translation.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <set>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <boost/algorithm/string/find.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/name_generator.hpp>
#include <boost/uuid/string_generator.hpp>

#include <unicode/uchar.h>

#include <sqlite3.h>

namespace
{

// ----------------------------------------------------------------
// SQLite helpers
// ----------------------------------------------------------------

[[noreturn]] void ThrowSQLite(sqlite3 *db, int rc)
{
    const char *msg = db ? sqlite3_errmsg(db) : sqlite3_errstr(rc);
    if (rc == SQLITE_CORRUPT || rc == SQLITE_NOTADB || rc == SQLITE_CANTOPEN)
        BOOST_THROW_EXCEPTION(Exception(wxString::Format(_("Translation memory database is corrupted: %s (%d)."),
                                                         wxString::FromUTF8(msg), rc)));
    BOOST_THROW_EXCEPTION(Exception(wxString::Format(_("Translation memory error: %s (%d)."),
                                                     wxString::FromUTF8(msg), rc)));
}

class Stmt
{
public:
    Stmt(sqlite3 *db, const char *sql) : m_db(db), m_st(nullptr)
    {
        int rc = sqlite3_prepare_v2(db, sql, -1, &m_st, nullptr);
        if (rc != SQLITE_OK)
            ThrowSQLite(db, rc);
    }
    ~Stmt() { sqlite3_finalize(m_st); }
    Stmt(const Stmt&) = delete;
    Stmt& operator=(const Stmt&) = delete;

    Stmt& bind(int i, const std::string& s)
        { check(sqlite3_bind_text(m_st, i, s.data(), (int)s.size(), SQLITE_TRANSIENT)); return *this; }
    Stmt& bind(int i, sqlite3_int64 v)
        { check(sqlite3_bind_int64(m_st, i, v)); return *this; }

    // true while there is a row
    bool step()
    {
        int rc = sqlite3_step(m_st);
        if (rc == SQLITE_ROW)
            return true;
        if (rc != SQLITE_DONE)
            ThrowSQLite(m_db, rc);
        return false;
    }
    void run() { step(); reset(); }
    void reset() { sqlite3_reset(m_st); sqlite3_clear_bindings(m_st); }

    std::string text(int col) const
    {
        auto p = (const char*)sqlite3_column_text(m_st, col);
        return p ? std::string(p, (size_t)sqlite3_column_bytes(m_st, col)) : std::string();
    }
    sqlite3_int64 integer(int col) const { return sqlite3_column_int64(m_st, col); }

private:
    void check(int rc) { if (rc != SQLITE_OK) ThrowSQLite(m_db, rc); }

    sqlite3 *m_db;
    sqlite3_stmt *m_st;
};

void Exec(sqlite3 *db, const char *sql)
{
    char *err = nullptr;
    int rc = sqlite3_exec(db, sql, nullptr, nullptr, &err);
    sqlite3_free(err);
    if (rc != SQLITE_OK)
        ThrowSQLite(db, rc);
}

#ifdef __AMIGA__
// AmigaOS has no fcntl() locks and its paths are not Unix paths:
// "VOLUME:dir/file" is absolute, anything else is relative to the current
// directory. The VFS is SQLite's "unix-none" (no locking: Poedit keeps the
// one connection) with its own full path names.
sqlite3_vfs s_amigaVfs;

int AmigaFullPathname(sqlite3_vfs*, const char *zName, int nOut, char *zOut)
{
    if (nOut < 2)
        return SQLITE_CANTOPEN;
    size_t len = 0;
    if (!strchr(zName, ':'))
    {
        if (!getcwd(zOut, nOut - 1))
            return SQLITE_CANTOPEN;
        len = strlen(zOut);
        if (len > 0 && zOut[len - 1] != ':' && zOut[len - 1] != '/' && *zName)
            zOut[len++] = '/';
    }
    const size_t n = strlen(zName);
    if (len + n + 1 > (size_t)nOut)
        return SQLITE_CANTOPEN;
    memcpy(zOut + len, zName, n + 1);
    return SQLITE_OK;
}

void UseAmigaVfs()
{
    static bool done = false;
    if (done)
        return;
    sqlite3_vfs *base = sqlite3_vfs_find("unix-none");
    if (!base)
        return;
    s_amigaVfs = *base;
    s_amigaVfs.zName = "amiga";
    s_amigaVfs.pNext = nullptr;
    s_amigaVfs.xFullPathname = AmigaFullPathname;
    sqlite3_vfs_register(&s_amigaVfs, 1);
    done = true;
}
#endif

// ----------------------------------------------------------------
// Words, as Lucene's StandardAnalyzer gives them
// ----------------------------------------------------------------

struct Token
{
    std::string term;   // UTF-8, lower case
    int pos;            // position, stop words counted
};

const std::set<std::string>& StopWords()
{
    static const std::set<std::string> s = {
        "a", "an", "and", "are", "as", "at", "be", "but", "by", "for", "if",
        "in", "into", "is", "it", "no", "not", "of", "on", "or", "such",
        "that", "the", "their", "then", "there", "these", "they", "this",
        "to", "was", "will", "with"
    };
    return s;
}

// one code point from UTF-8; bad bytes come out as themselves
UChar32 NextChar(const std::string& s, size_t& i)
{
    unsigned char c = (unsigned char)s[i++];
    if (c < 0x80)
        return c;
    int n = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
    UChar32 cp = c & (0x3F >> n);
    for (int k = 0; k < n && i < s.size(); k++)
    {
        unsigned char d = (unsigned char)s[i];
        if ((d & 0xC0) != 0x80)
            break;
        cp = (cp << 6) | (d & 0x3F);
        i++;
    }
    return cp;
}

void PutChar(std::string& out, UChar32 c)
{
    if (c < 0x80)
        out += (char)c;
    else if (c < 0x800)
    {
        out += (char)(0xC0 | (c >> 6));
        out += (char)(0x80 | (c & 0x3F));
    }
    else if (c < 0x10000)
    {
        out += (char)(0xE0 | (c >> 12));
        out += (char)(0x80 | ((c >> 6) & 0x3F));
        out += (char)(0x80 | (c & 0x3F));
    }
    else
    {
        out += (char)(0xF0 | (c >> 18));
        out += (char)(0x80 | ((c >> 12) & 0x3F));
        out += (char)(0x80 | ((c >> 6) & 0x3F));
        out += (char)(0x80 | (c & 0x3F));
    }
}

// a word of its own: CJK ideographs, hiragana, katakana
bool IsSingleCharWord(UChar32 c)
{
    return (c >= 0x3040 && c <= 0x30FF) || (c >= 0x31F0 && c <= 0x31FF) ||
           u_hasBinaryProperty(c, UCHAR_IDEOGRAPHIC);
}

bool IsWordChar(UChar32 c)
{
    if (c < 0x80)
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
    return u_isalnum(c) || u_charType(c) == U_NON_SPACING_MARK || u_charType(c) == U_COMBINING_SPACING_MARK;
}

UChar32 Lower(UChar32 c)
{
    if (c < 0x80)
        return (c >= 'A' && c <= 'Z') ? c + 32 : c;
    return u_tolower(c);
}

std::vector<Token> Tokenize(const std::string& utf8)
{
    std::vector<Token> out;
    const auto& stop = StopWords();
    int pos = -1;
    std::string word;
    auto flush = [&]()
    {
        if (word.empty())
            return;
        pos++;
        if (!stop.count(word))
            out.push_back({word, pos});
        word.clear();
    };
    size_t i = 0;
    while (i < utf8.size())
    {
        // apostrophes inside a word stay in it ("don't"), as in Lucene
        UChar32 c = NextChar(utf8, i);
        if (IsSingleCharWord(c))
        {
            flush();
            PutChar(word, c);
            flush();
        }
        else if (IsWordChar(c))
            PutChar(word, Lower(c));
        else if ((c == '\'' || c == 0x2019) && !word.empty() && i < utf8.size())
        {
            size_t j = i;
            UChar32 d = NextChar(utf8, j);
            if (IsWordChar(d) && !IsSingleCharWord(d))
                word += '\'';
            else
                flush();
        }
        else
            flush();
    }
    flush();
    return out;
}

// Lucene's phrase matching: the words in order, each at its position
// relative to the first, give or take `slop` places in all.
bool MatchesPhrase(const std::vector<Token>& query, const std::vector<Token>& doc, int slop)
{
    if (query.empty())
        return false;
    std::multimap<std::string, int> where;
    for (auto& t: doc)
        where.emplace(t.term, t.pos);
    const int base = query[0].pos;
    auto range0 = where.equal_range(query[0].term);
    for (auto it0 = range0.first; it0 != range0.second; ++it0)
    {
        int used = 0;
        bool ok = true;
        for (size_t k = 1; k < query.size() && ok; k++)
        {
            const int want = it0->second + (query[k].pos - base);
            int best = -1;
            auto r = where.equal_range(query[k].term);
            for (auto it = r.first; it != r.second; ++it)
            {
                int d = std::abs(it->second - want);
                if (best < 0 || d < best)
                    best = d;
            }
            if (best < 0 || used + best > slop)
                ok = false;
            else
                used += best;
        }
        if (ok)
            return true;
    }
    return false;
}

// ----------------------------------------------------------------
// Search constants (as Poedit's)
// ----------------------------------------------------------------

// Max. number of hits returned from this API
const int MAX_RESULTS = 10;
// Max. number of candidate entries looked at per search
const int QUERY_MAX_DOCS = 500;
// Normalized score that must be met for a suggestion to be shown
const double QUALITY_THRESHOLD = 0.6;
// Maximum allowed difference in phrase length, in #terms
const int MAX_ALLOWED_LENGTH_DIFFERENCE = 3;
// weight of a hit in the short form of the language (cs for cs_CZ etc.)
const double SECONDARY_LANG_BOOST = 0.85;

void AddOrUpdateResult(SuggestionsList& all, Suggestion&& r)
{
    // hits with the same translation but a different source keep the best score
    auto found = std::find_if(all.begin(), all.end(),
                              [&r](const Suggestion& x){ return x.text == r.text; });
    if (found == all.end())
        all.push_back(std::move(r));
    else if (r.score > found->score)
        *found = r;
}

void postprocess_results(SuggestionsList& results)
{
    results.erase(std::remove_if(results.begin(), results.end(),
                                 [](const auto& r) { return r.score < 0.3; }),
                  results.end());
    std::stable_sort(results.begin(), results.end());
    if (results.size() > MAX_RESULTS)
        results.resize(MAX_RESULTS);
}

struct Entry
{
    std::string uuid, srclang, lang, source, trans;
    time_t created;
    int ntok;
    bool secondaryLang;
};

} // anonymous namespace


// ----------------------------------------------------------------
// TranslationMemoryImpl
// ----------------------------------------------------------------

class TranslationMemoryImpl;
std::shared_ptr<TranslationMemory::Writer> MakeWriter(TranslationMemoryImpl& tm);

class TranslationMemoryImpl
{
public:
    TranslationMemoryImpl() : m_db(nullptr), m_inTransaction(false) { Init(); }
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
    void BeginIfNeeded();

    // entries in srclang and lang (or its short form) that have at least
    // `need` of the words, fewest words first
    std::vector<Entry> Candidates(const Language& srclang, const Language& lang,
                                  const std::vector<Token>& tokens, int need);

    template<typename T>
    void PerformSearch(const Language& srclang, const Language& lang,
                       const std::vector<Token>& tokens, int need, int slop, bool phrase,
                       const std::string& exactSource,
                       double scoreThreshold, double scoreScaling, T callback);

    sqlite3 *m_db;
    bool m_inTransaction;
    std::recursive_mutex m_mutex;
    std::shared_ptr<TranslationMemory::Writer> m_writerAPI;
};


std::wstring TranslationMemoryImpl::GetDatabaseDir()
{
    wxString data;
#if defined(__UNIX__) && !defined(__WXOSX__) && !defined(__AMIGA__)
    if ( !wxGetEnv("XDG_DATA_HOME", &data) )
        data = wxGetHomeDir() + "/.local/share";
    data += "/poedit";
#else
    data = wxStandardPaths::Get().GetUserDataDir();
#endif

    // ensure the parent directory exists:
    wxFileName::Mkdir(data, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);

    data += wxFILE_SEP_PATH;
    data += "TranslationMemory";

    return data.ToStdWstring();
}


void TranslationMemoryImpl::Init()
{
    const wxString dir(GetDatabaseDir());
    wxFileName::Mkdir(dir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
    const wxString path = dir + wxFILE_SEP_PATH + "tm.sqlite";

    int rc = sqlite3_initialize();
    if (rc != SQLITE_OK)
        ThrowSQLite(nullptr, rc);
#ifdef __AMIGA__
    UseAmigaVfs();
#endif

    rc = sqlite3_open_v2(path.utf8_str(), &m_db,
                         SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
    if (rc != SQLITE_OK)
    {
        sqlite3_close(m_db);
        m_db = nullptr;
        ThrowSQLite(nullptr, rc);
    }

    Exec(m_db,
         "PRAGMA journal_mode=DELETE;"
         "PRAGMA synchronous=NORMAL;"
         "PRAGMA temp_store=MEMORY;"
         "CREATE TABLE IF NOT EXISTS entries("
         "  id INTEGER PRIMARY KEY,"
         "  uuid TEXT NOT NULL UNIQUE,"
         "  srclang TEXT NOT NULL,"
         "  lang TEXT NOT NULL,"
         "  source TEXT NOT NULL,"
         "  trans TEXT NOT NULL,"
         "  created INTEGER NOT NULL,"
         "  ntok INTEGER NOT NULL);"
         "CREATE INDEX IF NOT EXISTS entries_lang ON entries(srclang, lang);"
         "CREATE TABLE IF NOT EXISTS terms("
         "  term TEXT NOT NULL,"
         "  entry INTEGER NOT NULL,"
         "  pos INTEGER NOT NULL,"
         "  PRIMARY KEY(term, entry, pos)) WITHOUT ROWID;"
         "CREATE INDEX IF NOT EXISTS terms_entry ON terms(entry);");

    // checks the file really is ours (a damaged one throws here)
    Stmt(m_db, "SELECT COUNT(*) FROM entries").step();

    m_writerAPI = MakeWriter(*this);
}


TranslationMemoryImpl::~TranslationMemoryImpl()
{
    // uncommitted changes are committed on close, as the Writer promises
    m_writerAPI.reset();
    if (m_db)
    {
        if (m_inTransaction)
            sqlite3_exec(m_db, "COMMIT", nullptr, nullptr, nullptr);
        sqlite3_close(m_db);
    }
}


void TranslationMemoryImpl::BeginIfNeeded()
{
    if (!m_inTransaction)
    {
        Exec(m_db, "BEGIN");
        m_inTransaction = true;
    }
}


void TranslationMemoryImpl::Commit()
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_inTransaction)
    {
        Exec(m_db, "COMMIT");
        m_inTransaction = false;
    }
}


void TranslationMemoryImpl::Rollback()
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_inTransaction)
    {
        Exec(m_db, "ROLLBACK");
        m_inTransaction = false;
    }
}


void TranslationMemoryImpl::Insert(const Language& srclang, const Language& lang,
                                   const std::wstring& source, const std::wstring& trans,
                                   time_t creationTime)
{
    if (!lang.IsValid() || !srclang.IsValid() || lang == srclang)
        return;

    if (creationTime == 0)
        creationTime = time(NULL);

    // the same unique id Poedit gives an entry
    static const boost::uuids::uuid s_namespace =
      boost::uuids::string_generator()("6e3f73c5-333f-4171-9d43-954c372a8a02");
    boost::uuids::name_generator gen(s_namespace);

    std::wstring itemId(srclang.WCode());
    itemId += lang.WCode();
    itemId += source;
    itemId += trans;
    const std::string uuid = boost::uuids::to_string(gen(itemId));

    const std::string src8 = str::to_utf8(source);
    const auto tokens = Tokenize(src8);

    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    BeginIfNeeded();

    Delete(uuid);

    Stmt ins(m_db, "INSERT INTO entries(uuid, srclang, lang, source, trans, created, ntok) "
                   "VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7)");
    ins.bind(1, uuid).bind(2, srclang.Code()).bind(3, lang.Code())
       .bind(4, src8).bind(5, str::to_utf8(trans))
       .bind(6, (sqlite3_int64)creationTime).bind(7, (sqlite3_int64)tokens.size());
    ins.run();
    const sqlite3_int64 id = sqlite3_last_insert_rowid(m_db);

    Stmt term(m_db, "INSERT OR IGNORE INTO terms(term, entry, pos) VALUES(?1, ?2, ?3)");
    for (auto& t: tokens)
    {
        term.bind(1, t.term).bind(2, id).bind(3, (sqlite3_int64)t.pos);
        term.run();
    }
}


void TranslationMemoryImpl::Delete(const std::string& uuid)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    BeginIfNeeded();
    Stmt delTerms(m_db, "DELETE FROM terms WHERE entry IN (SELECT id FROM entries WHERE uuid=?1)");
    delTerms.bind(1, uuid).run();
    Stmt del(m_db, "DELETE FROM entries WHERE uuid=?1");
    del.bind(1, uuid).run();
}


void TranslationMemoryImpl::DeleteAll()
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    BeginIfNeeded();
    Exec(m_db, "DELETE FROM terms; DELETE FROM entries;");
}


std::vector<Entry> TranslationMemoryImpl::Candidates(const Language& srclang, const Language& lang,
                                                     const std::vector<Token>& tokens, int need)
{
    std::vector<Entry> out;

    std::vector<std::string> terms;
    for (auto& t: tokens)
        if (std::find(terms.begin(), terms.end(), t.term) == terms.end())
            terms.push_back(t.term);
    if (terms.empty())
        return out;
    need = std::min(need, (int)terms.size());

    // the primary language and its short form: "cs" also finds "cs_*",
    // "cs_CZ" also finds "cs"
    const std::string full = lang.Code();
    const std::string shortLang = lang.Lang();
    const bool isShort = (full == shortLang);

    std::string sql =
        "SELECT e.uuid, e.srclang, e.lang, e.source, e.trans, e.created, e.ntok "
        "FROM entries e JOIN ("
        "  SELECT entry, COUNT(DISTINCT term) AS n FROM terms WHERE term IN (";
    for (size_t i = 0; i < terms.size(); i++)
        sql += (i ? ",?" : "?") + std::to_string(i + 4);
    sql += ") GROUP BY entry HAVING n >= ?3) m ON m.entry = e.id "
           "WHERE e.srclang = ?1 AND (e.lang = ?2 OR ";
    sql += isShort ? "e.lang LIKE ?2 || '\\_%' ESCAPE '\\'" : "e.lang = ?" + std::to_string(terms.size() + 4);
    sql += ") ORDER BY m.n DESC, e.ntok ASC LIMIT " + std::to_string(QUERY_MAX_DOCS);

    Stmt st(m_db, sql.c_str());
    st.bind(1, srclang.Code()).bind(2, full).bind(3, (sqlite3_int64)need);
    for (size_t i = 0; i < terms.size(); i++)
        st.bind((int)i + 4, terms[i]);
    if (!isShort)
        st.bind((int)terms.size() + 4, shortLang);

    while (st.step())
    {
        Entry e;
        e.uuid = st.text(0);
        e.srclang = st.text(1);
        e.lang = st.text(2);
        e.source = st.text(3);
        e.trans = st.text(4);
        e.created = (time_t)st.integer(5);
        e.ntok = (int)st.integer(6);
        e.secondaryLang = (e.lang != full);
        out.push_back(std::move(e));
    }
    return out;
}


// Scores as Lucene ranks these hits, relative to the best one (Poedit
// divides by the top score): each word's weight (rarer words weigh more),
// how many of the words matched, and a shorter text scoring higher.
template<typename T>
void TranslationMemoryImpl::PerformSearch(const Language& srclang, const Language& lang,
                                          const std::vector<Token>& tokens, int need, int slop, bool phrase,
                                          const std::string& exactSource,
                                          double scoreThreshold, double scoreScaling, T callback)
{
    auto cands = Candidates(srclang, lang, tokens, need);
    if (cands.empty())
        return;

    // word weights: idf = 1 + ln(N / (df + 1))
    std::map<std::string, double> idf;
    {
        Stmt total(m_db, "SELECT COUNT(*) FROM entries");
        total.step();
        const double N = (double)std::max<sqlite3_int64>(1, total.integer(0));
        Stmt df(m_db, "SELECT COUNT(DISTINCT entry) FROM terms WHERE term=?1");
        for (auto& t: tokens)
        {
            if (idf.count(t.term))
                continue;
            df.bind(1, t.term);
            df.step();
            idf[t.term] = 1.0 + std::log(N / (double)(df.integer(0) + 1));
            df.reset();
        }
    }
    double allWeight = 0;
    for (auto& w: idf)
        allWeight += w.second * w.second;

    struct Hit { size_t i; double raw; };
    std::vector<Hit> hits;
    double maxRaw = 0;
    for (size_t i = 0; i < cands.size(); i++)
    {
        const auto& e = cands[i];
        const auto doc = Tokenize(e.source);
        if (phrase && !MatchesPhrase(tokens, doc, slop))
            continue;

        std::set<std::string> have;
        for (auto& t: doc)
            have.insert(t.term);
        double w = 0;
        int matched = 0;
        for (auto& x: idf)
            if (have.count(x.first))
            {
                w += x.second * x.second;
                matched++;
            }
        const double coord = idf.empty() ? 0 : (double)matched / (double)idf.size();
        double raw = (allWeight > 0 ? w / allWeight : 0) * coord / std::sqrt((double)std::max(1, (int)doc.size()));
        if (e.secondaryLang)
            raw *= SECONDARY_LANG_BOOST;
        hits.push_back({i, raw});
        maxRaw = std::max(maxRaw, raw);
    }
    if (maxRaw <= 0)
        return;

    for (auto& h: hits)
    {
        const auto& e = cands[h.i];
        double score = h.raw / maxRaw;
        if (score < scoreThreshold)
            continue;

        if (e.source == exactSource)
        {
            score = 1.0;
        }
        else
        {
            // penalize large length mismatch using ratio; goes to ~0 for tiny-vs-huge
            const std::wstring a = str::to_wstring(exactSource), b = str::to_wstring(e.source);
            double len1 = (double)std::max<size_t>(1, a.size());
            double len2 = (double)std::max<size_t>(1, b.size());
            if (std::max(len1, len2) > 3.0 * std::min(len1, len2))
                continue;
            double lr = std::log(std::max(len1, len2) / std::min(len1, len2));
            const auto penalty = std::exp(-2.0 * lr * lr);
            score *= std::min(penalty, 0.95) * scoreScaling;
        }
        callback(e, score);
    }
}


SuggestionsList TranslationMemoryImpl::Search(const Language& srclang,
                                              const Language& lang,
                                              const std::wstring& source)
{
    try
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        SuggestionsList results;

        const std::string src8 = str::to_utf8(source);
        const auto tokens = Tokenize(src8);
        if (tokens.empty())
            return results;
        const int nterms = (int)tokens.size();

        auto add = [&results](const Entry& e, double score)
        {
            Suggestion r {str::to_wstring(e.trans), score, int(e.created)};
            r.id = e.uuid;
            AddOrUpdateResult(results, std::move(r));
        };

        // Try exact phrase first:
        PerformSearch(srclang, lang, tokens, nterms, 0, true, src8, QUALITY_THRESHOLD, 1.0, add);
        postprocess_results(results);
        if (!results.empty())
            return results;

        // Then, if no matches were found, permit being a bit sloppy:
        PerformSearch(srclang, lang, tokens, nterms, 1, true, src8, QUALITY_THRESHOLD, 0.8, add);
        postprocess_results(results);
        if (!results.empty())
            return results;

        // As the last resort, try terms search:
        std::set<std::string> distinct;
        for (auto& t: tokens)
            distinct.insert(t.term);
        const int need = std::max(1, (int)distinct.size() - MAX_ALLOWED_LENGTH_DIFFERENCE);
        PerformSearch(srclang, lang, tokens, need, 0, false, src8, QUALITY_THRESHOLD, 0.7,
                      [&](const Entry& e, double score)
                      {
                          if (std::abs(e.ntok - nterms) <= MAX_ALLOWED_LENGTH_DIFFERENCE)
                              add(e, score);
                      });

        postprocess_results(results);
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
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    const std::string src8 = str::to_utf8(sourcePhrase);
    const auto tokens = Tokenize(src8);
    if (tokens.empty())
        return;

    PerformSearch(srclang, lang, tokens, (int)tokens.size(), 0, true, src8, 0.0, 1.0,
                  [&](const Entry& e, double /*score*/)
                  {
                      const std::wstring sourceText = str::to_wstring(e.source);
                      if (boost::algorithm::ifind_first(sourceText, sourcePhrase))
                      {
                          destination.Insert(srclang, lang, sourceText, str::to_wstring(e.trans), e.created);
                      }
                  });
}


void TranslationMemoryImpl::ExportData(TranslationMemory::IOInterface& destination)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    Stmt count(m_db, "SELECT COUNT(*) FROM entries");
    count.step();
    Progress progress((int)count.integer(0));

    Stmt st(m_db, "SELECT srclang, lang, source, trans, created FROM entries ORDER BY id");
    while (st.step())
    {
        progress.increment();
        destination.Insert(Language::TryParse(st.text(0)),
                           Language::TryParse(st.text(1)),
                           str::to_wstring(st.text(2)),
                           str::to_wstring(st.text(3)),
                           (time_t)st.integer(4));
    }
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
    Stmt count(m_db, "SELECT COUNT(*) FROM entries");
    count.step();
    numDocs = (long)count.integer(0);
    fileSize = (long)wxDir::GetTotalSize(GetDatabaseDir()).GetValue();
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
