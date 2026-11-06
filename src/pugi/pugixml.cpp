/*
 *  The pugixml calls Poedit uses, on our own XML parser (OXP, GFOpenXML):
 *  the tree, parsing through OXP, the writer and a small XPath.
 *  See pugixml.hpp.
 *
 *  Copyright (c) 2026 AmiCradle Team
 */

#include "pugixml.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <list>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

#include <oxp.h>

namespace pugi
{

struct xml_attribute_struct
{
    std::string name, value;
    xml_attribute_struct *prev = nullptr, *next = nullptr;
};

struct xml_node_struct
{
    explicit xml_node_struct(xml_node_type t) : type(t) {}
    ~xml_node_struct()
    {
        for (xml_attribute_struct *a = first_attr, *n; a; a = n)
        {
            n = a->next;
            delete a;
        }
        for (xml_node_struct *c = first_child, *n; c; c = n)
        {
            n = c->next;
            delete c;
        }
    }
    xml_node_type type;
    std::string name, value;
    xml_attribute_struct *first_attr = nullptr, *last_attr = nullptr;
    xml_node_struct *parent = nullptr, *first_child = nullptr, *last_child = nullptr;
    xml_node_struct *prev = nullptr, *next = nullptr;
};

namespace
{

bool has_name(xml_node_type t) { return t == node_element || t == node_pi || t == node_declaration; }
bool has_value(xml_node_type t)
{
    return t == node_pcdata || t == node_cdata || t == node_comment || t == node_pi || t == node_doctype;
}
bool allows_children(xml_node_struct *n) { return n && (n->type == node_document || n->type == node_element); }
bool allows_attributes(xml_node_struct *n) { return n && (n->type == node_element || n->type == node_declaration); }

// a child of this type may go in parent
bool allows_child(xml_node_struct *parent, xml_node_type t)
{
    if (!allows_children(parent) || t == node_null || t == node_document)
        return false;
    if ((t == node_declaration || t == node_doctype) && parent->type != node_document)
        return false;
    return true;
}

void link_after(xml_node_struct *child, xml_node_struct *parent, xml_node_struct *after)
{
    child->parent = parent;
    child->prev = after;
    child->next = after ? after->next : parent->first_child;
    if (child->next)
        child->next->prev = child;
    else
        parent->last_child = child;
    if (after)
        after->next = child;
    else
        parent->first_child = child;
}

void link_last(xml_node_struct *child, xml_node_struct *parent) { link_after(child, parent, parent->last_child); }

void unlink(xml_node_struct *n)
{
    xml_node_struct *p = n->parent;
    if (n->prev) n->prev->next = n->next; else p->first_child = n->next;
    if (n->next) n->next->prev = n->prev; else p->last_child = n->prev;
    n->parent = n->prev = n->next = nullptr;
}

void attr_link_after(xml_attribute_struct *a, xml_node_struct *n, xml_attribute_struct *after)
{
    a->prev = after;
    a->next = after ? after->next : n->first_attr;
    if (a->next) a->next->prev = a; else n->last_attr = a;
    if (after) after->next = a; else n->first_attr = a;
}

void attr_unlink(xml_attribute_struct *a, xml_node_struct *n)
{
    if (a->prev) a->prev->next = a->next; else n->first_attr = a->next;
    if (a->next) a->next->prev = a->prev; else n->last_attr = a->prev;
}

bool owns_attr(xml_node_struct *n, xml_attribute_struct *a)
{
    for (auto *x = n ? n->first_attr : nullptr; x; x = x->next)
        if (x == a)
            return true;
    return false;
}

xml_node_struct *copy_tree(const xml_node_struct *src)
{
    auto *n = new xml_node_struct(src->type);
    n->name = src->name;
    n->value = src->value;
    for (auto *a = src->first_attr; a; a = a->next)
    {
        auto *c = new xml_attribute_struct;
        c->name = a->name;
        c->value = a->value;
        attr_link_after(c, n, n->last_attr);
    }
    for (auto *c = src->first_child; c; c = c->next)
        link_last(copy_tree(c), n);
    return n;
}

const char *sz(const std::string& s) { return s.c_str(); }

// ---------------------------------------------------------------- parsing (OXP)

struct Builder
{
    xml_node_struct *root;          // where the nodes go
    xml_node_struct *cur;
    unsigned options;
    const char *raw;                // the input, for doctypes and top-level spacing
    size_t rawLen;
    bool rawUtf8;
    oxp_parser *parser = nullptr;
    std::string text;               // characters not yet in a node
    bool haveText = false;
    int elements = 0;
    bool keepTopSpace;              // document mode: top-level spacing kept as text
    std::list<std::string> unknownEntities;   // stable addresses

    void flush_text()
    {
        if (!haveText)
            return;
        haveText = false;
        bool ws = true;
        for (unsigned char c: text)
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
            {
                ws = false;
                break;
            }
        if (ws && !(options & parse_ws_pcdata))
        {
            if (!((options & parse_ws_pcdata_single) && !cur->first_child))
            {
                text.clear();
                return;
            }
        }
        if (cur->type == node_document && !(options & parse_fragment))
        {
            text.clear();
            return;
        }
        auto *n = new xml_node_struct(node_pcdata);
        n->value.swap(text);
        link_last(n, cur);
        text.clear();
    }

    // the spacing before the markup that starts here, at the top level
    void top_space()
    {
        if (!keepTopSpace || cur != root || !rawUtf8 || !parser)
            return;
        size_t at = oxp_bytes_consumed(parser);
        top_space_before(at);
    }
    void top_space_before(size_t at)
    {
        if (at > rawLen)
            return;
        size_t b = at;
        while (b > 0 && (raw[b - 1] == ' ' || raw[b - 1] == '\t' || raw[b - 1] == '\n' || raw[b - 1] == '\r'))
            b--;
        if (b == at || (b == 0 && at == rawLen))
            return;
        std::string s;
        for (size_t i = b; i < at; i++)
        {
            if (raw[i] == '\r')
            {
                s += '\n';
                if (i + 1 < at && raw[i + 1] == '\n')
                    i++;
            }
            else
                s += raw[i];
        }
        auto *n = new xml_node_struct(node_pcdata);
        n->value = s;
        link_last(n, root);
    }

    // <!DOCTYPE value>: the text between the keyword and the closing '>'
    std::string doctype_value(size_t at, const char *name)
    {
        if (rawUtf8 && at + 9 <= rawLen && memcmp(raw + at, "<!DOCTYPE", 9) == 0)
        {
            size_t i = at + 9, depth = 0;
            char q = 0;
            while (i < rawLen)
            {
                char c = raw[i];
                if (q) { if (c == q) q = 0; }
                else if (c == '"' || c == '\'') q = c;
                else if (c == '[') depth++;
                else if (c == ']') { if (depth) depth--; }
                else if (c == '>' && depth == 0) break;
                i++;
            }
            size_t s = at + 9;
            while (s < i && (raw[s] == ' ' || raw[s] == '\t' || raw[s] == '\n' || raw[s] == '\r'))
                s++;
            return std::string(raw + s, i - s);
        }
        return name ? name : "";
    }
};

std::string qname(const char *local, const char *prefix)
{
    if (prefix && *prefix)
        return std::string(prefix) + ":" + local;
    return local;
}

void cb_start_document(void *ud, const char *version, const char *encoding, int standalone)
{
    auto *b = (Builder*)ud;
    if (!version || !(b->options & parse_declaration))
        return;
    auto *n = new xml_node_struct(node_declaration);
    n->name = "xml";
    auto add = [n](const char *k, const char *v)
    {
        auto *a = new xml_attribute_struct;
        a->name = k;
        a->value = v;
        attr_link_after(a, n, n->last_attr);
    };
    add("version", version);
    if (encoding)
        add("encoding", encoding);
    if (standalone >= 0)
        add("standalone", standalone ? "yes" : "no");
    link_last(n, b->cur);
}

void cb_doctype(void *ud, const char *name, const char *, const char *, int)
{
    auto *b = (Builder*)ud;
    b->flush_text();
    b->top_space();
    if (!(b->options & parse_doctype))
        return;
    auto *n = new xml_node_struct(node_doctype);
    n->value = b->doctype_value(b->parser ? oxp_bytes_consumed(b->parser) : (size_t)-1, name);
    link_last(n, b->cur);
}

void cb_start_element(void *ud, const char *local, const char *prefix, const char *,
                      int, const oxp_ns *, int nattrs, const oxp_attr *attrs)
{
    auto *b = (Builder*)ud;
    b->flush_text();
    b->top_space();
    auto *n = new xml_node_struct(node_element);
    n->name = qname(local, prefix);
    for (int i = 0; i < nattrs; i++)
    {
        if (attrs[i].defaulted)
            continue;
        auto *a = new xml_attribute_struct;
        a->name = qname(attrs[i].local, attrs[i].prefix);
        a->value.assign(attrs[i].value, attrs[i].value_len);
        attr_link_after(a, n, n->last_attr);
    }
    link_last(n, b->cur);
    b->cur = n;
    b->elements++;
}

void cb_end_element(void *ud, const char *, const char *, const char *)
{
    auto *b = (Builder*)ud;
    b->flush_text();
    if (b->cur != b->root)
        b->cur = b->cur->parent;
}

void cb_characters(void *ud, const char *text, size_t len)
{
    auto *b = (Builder*)ud;
    b->text.append(text, len);
    b->haveText = true;
}

void cb_cdata(void *ud, const char *text, size_t len)
{
    auto *b = (Builder*)ud;
    if (!(b->options & parse_cdata))
    {
        cb_characters(ud, text, len);
        return;
    }
    b->flush_text();
    auto *n = new xml_node_struct(node_cdata);
    n->value.assign(text, len);
    link_last(n, b->cur);
}

void cb_comment(void *ud, const char *text)
{
    auto *b = (Builder*)ud;
    b->flush_text();
    b->top_space();
    if (!(b->options & parse_comments))
        return;
    auto *n = new xml_node_struct(node_comment);
    n->value = text;
    link_last(n, b->cur);
}

void cb_pi(void *ud, const char *target, const char *data)
{
    auto *b = (Builder*)ud;
    b->flush_text();
    b->top_space();
    if (!(b->options & parse_pi))
        return;
    auto *n = new xml_node_struct(node_pi);
    n->name = target;
    n->value = data ? data : "";
    link_last(n, b->cur);
}

// pugixml leaves an entity it does not know as it is written (&name;)
const char *cb_resolve_entity(void *ud, const char *name)
{
    auto *b = (Builder*)ud;
    if (!(b->options & parse_escapes))
        return nullptr;
    b->unknownEntities.push_back(std::string("&amp;") + name + ";");
    return b->unknownEntities.back().c_str();
}

xml_parse_status status_of(oxp_code c)
{
    switch (c)
    {
        case OXP_OK: return status_ok;
        case OXP_E_NOMEM: return status_out_of_memory;
        case OXP_E_TAG_MISMATCH:
        case OXP_E_UNCLOSED: return status_end_element_mismatch;
        case OXP_E_DUP_ATTR:
        case OXP_E_NS_PREFIX: return status_bad_attribute;
        case OXP_E_ENTITY_UNDECLARED:
        case OXP_E_ENTITY_RECURSION:
        case OXP_E_ENTITY_LIMIT:
        case OXP_E_ENTITY_UNPARSED:
        case OXP_E_ENTITY_EXTERNAL:
        case OXP_E_CHAR:
        case OXP_E_ENCODING: return status_bad_pcdata;
        default: return status_unrecognized_tag;
    }
}

oxp_encoding oxp_enc(xml_encoding e)
{
    switch (e)
    {
        case encoding_utf8: return OXP_ENC_UTF8;
        case encoding_utf16_le: return OXP_ENC_UTF16LE;
        case encoding_utf16_be: return OXP_ENC_UTF16BE;
        case encoding_latin1: return OXP_ENC_LATIN1;
        default: return OXP_ENC_AUTO;
    }
}

bool looks_utf8(const char *d, size_t n, xml_encoding enc)
{
    if (enc == encoding_utf8)
        return true;
    if (enc != encoding_auto)
        return enc == encoding_latin1;   // byte offsets are the same
    if (n >= 2 && ((unsigned char)d[0] == 0xFE || (unsigned char)d[0] == 0xFF))
        return false;   // UTF-16 BOM
    if (n >= 2 && (d[0] == 0 || d[1] == 0))
        return false;   // UTF-16 without a BOM
    return true;
}

// parses data into `into` (a document or an element)
xml_parse_result parse_into(xml_node_struct *into, const void *data, size_t size,
                            unsigned options, xml_encoding encoding, bool fragment)
{
    xml_parse_result res;
    res.encoding = encoding_utf8;

    Builder b;
    b.root = b.cur = into;
    b.options = options;
    b.raw = (const char*)data;
    b.rawLen = size;
    b.rawUtf8 = looks_utf8(b.raw, size, encoding);
    b.keepTopSpace = !fragment && into->type == node_document &&
                     (options & parse_ws_pcdata) && (options & parse_fragment);

    // a document holding no element: nothing to parse in document mode
    bool onlySpace = true;
    for (size_t i = 0; i < size && onlySpace; i++)
    {
        char c = b.raw[i];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
            onlySpace = false;
    }
    if (onlySpace && !fragment)
    {
        res.status = (options & parse_fragment) ? status_ok : status_no_document_element;
        return res;
    }

    oxp_handler h;
    memset(&h, 0, sizeof(h));
    h.start_document = cb_start_document;
    h.doctype = cb_doctype;
    h.start_element = cb_start_element;
    h.end_element = cb_end_element;
    h.characters = cb_characters;
    h.cdata = cb_cdata;
    h.comment = cb_comment;
    h.pi = cb_pi;
    h.resolve_entity = cb_resolve_entity;

    oxp_parser *p = oxp_new(&h, &b, nullptr);
    if (!p)
    {
        res.status = status_out_of_memory;
        return res;
    }
    b.parser = p;
    oxp_set_options(p, OXP_OPT_NO_NAMESPACES | (fragment ? OXP_OPT_FRAGMENT : 0));
    if (encoding != encoding_auto)
        oxp_set_encoding(p, oxp_enc(encoding));
    oxp_code rc = oxp_parse(p, data, size, 1);
    b.flush_text();
    if (rc == OXP_OK && b.keepTopSpace && b.rawUtf8)
        b.top_space_before(size);
    res.offset = (ptrdiff_t)oxp_bytes_consumed(p);
    oxp_free(p);

    if (rc != OXP_OK && b.elements == 0 && !fragment)
        res.status = status_no_document_element;
    else
        res.status = status_of(rc);
    return res;
}

// ---------------------------------------------------------------- writing

struct Out
{
    xml_writer& w;
    std::string buf;
    explicit Out(xml_writer& writer) : w(writer) {}
    ~Out() { flush(); }
    void flush() { if (!buf.empty()) { w.write(buf.data(), buf.size()); buf.clear(); } }
    void put(const std::string& s) { buf += s; if (buf.size() > 8192) flush(); }
    void put(const char *s) { buf += s; if (buf.size() > 8192) flush(); }
    void put(char c) { buf += c; }
};

void escaped(Out& o, const std::string& s, bool attr, unsigned flags)
{
    if (flags & format_no_escapes)
    {
        o.put(s);
        return;
    }
    for (unsigned char c: s)
    {
        switch (c)
        {
            case '&': o.put("&amp;"); break;
            case '<': o.put("&lt;"); break;
            case '>': o.put("&gt;"); break;
            case '"':
                if (attr && !(flags & format_attribute_single_quote)) o.put("&quot;"); else o.put('"');
                break;
            case '\'':
                if (attr && (flags & format_attribute_single_quote)) o.put("&apos;"); else o.put('\'');
                break;
            default:
                if (c < 32 && (attr || (c != '\t' && c != '\n' && c != '\r')))
                {
                    if (!(flags & format_skip_control_chars))
                    {
                        char t[8];
                        snprintf(t, sizeof(t), "&#%u%u;", c / 10, c % 10);
                        o.put(t);
                    }
                }
                else
                    o.put((char)c);
        }
    }
}

void write_attrs(Out& o, const xml_node_struct *n, unsigned flags)
{
    const char q = (flags & format_attribute_single_quote) ? '\'' : '"';
    for (auto *a = n->first_attr; a; a = a->next)
    {
        o.put(' ');
        o.put(a->name);
        o.put('=');
        o.put(q);
        escaped(o, a->value, true, flags);
        o.put(q);
    }
}

void write_simple(Out& o, const xml_node_struct *n, unsigned flags)
{
    switch (n->type)
    {
        case node_pcdata:
            escaped(o, n->value, false, flags);
            break;
        case node_cdata:
        {
            o.put("<![CDATA[");
            size_t from = 0, at;
            while ((at = n->value.find("]]>", from)) != std::string::npos)
            {
                o.put(n->value.substr(from, at + 2 - from));
                o.put("]]><![CDATA[");
                from = at + 2;
            }
            o.put(n->value.substr(from));
            o.put("]]>");
            break;
        }
        case node_comment:
            o.put("<!--");
            o.put(n->value);
            o.put("-->");
            break;
        case node_pi:
            o.put("<?");
            o.put(n->name);
            if (!n->value.empty())
            {
                o.put(' ');
                o.put(n->value);
            }
            o.put("?>");
            break;
        case node_declaration:
            o.put("<?");
            o.put(n->name);
            write_attrs(o, n, flags);
            o.put("?>");
            break;
        case node_doctype:
            o.put("<!DOCTYPE");
            if (!n->value.empty())
            {
                o.put(' ');
                o.put(n->value);
            }
            o.put('>');
            break;
        default:
            break;
    }
}

// pugixml's layout: each node on its own line, indented, unless it sits
// next to text; format_raw writes the tree exactly as it is
void write_tree(Out& o, const xml_node_struct *root, const char *indent, unsigned flags, unsigned depth)
{
    const bool pretty = (flags & format_indent) && !(flags & format_raw);
    const std::string ind = (pretty && indent) ? indent : "";
    enum { NL = 1, IND = 2 };
    unsigned state = IND;
    auto lead = [&]()
    {
        if ((state & NL) && !(flags & format_raw))
            o.put('\n');
        if ((state & IND) && !ind.empty())
            for (unsigned i = 0; i < depth; i++)
                o.put(ind);
    };

    const xml_node_struct *n = root;
    do
    {
        bool descend = false;
        if (n->type == node_pcdata || n->type == node_cdata)
        {
            write_simple(o, n, flags);
            state = 0;
        }
        else
        {
            lead();
            if (n->type == node_element)
            {
                state = NL | IND;
                o.put('<');
                o.put(n->name);
                write_attrs(o, n, flags);
                if (!n->first_child)
                {
                    if (flags & format_no_empty_element_tags)
                    {
                        o.put("></");
                        o.put(n->name);
                        o.put('>');
                    }
                    else
                    {
                        if (!(flags & format_raw))
                            o.put(' ');
                        o.put("/>");
                    }
                }
                else
                {
                    o.put('>');
                    descend = true;
                }
            }
            else if (n->type == node_document)
            {
                state = IND;
                descend = n->first_child != nullptr;
            }
            else
            {
                write_simple(o, n, flags);
                state = NL | IND;
            }
        }

        if (descend)
        {
            n = n->first_child;
            if (n->parent->type == node_element)
                depth++;
            continue;
        }

        // on to the next node, closing elements on the way up
        while (n != root)
        {
            if (n->next)
            {
                n = n->next;
                break;
            }
            n = n->parent;
            if (n->type == node_element)
            {
                depth--;
                lead();
                o.put("</");
                o.put(n->name);
                o.put('>');
                state = NL | IND;
            }
        }
    }
    while (n != root);

    if ((state & NL) && !(flags & format_raw))
        o.put('\n');
}

class string_writer : public xml_writer
{
public:
    std::string s;
    void write(const void *data, size_t size) override { s.append((const char*)data, size); }
};

// ---------------------------------------------------------------- XPath

struct XNode
{
    xml_node_struct *node;
    xml_attribute_struct *attr;   // with node as its element
};

class XPath
{
public:
    explicit XPath(const char *q) : s(q ? q : ""), i(0), ok(true) {}

    std::vector<XNode> run(xml_node_struct *ctx)
    {
        std::vector<XNode> r = path(std::vector<XNode>{{ctx, nullptr}});
        skip();
        if (i != s.size())
            ok = false;
        if (!ok)
            r.clear();
        return r;
    }

private:
    std::string s;
    size_t i;
    bool ok;

    void skip() { while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n')) i++; }
    bool eat(const char *t)
    {
        skip();
        size_t n = strlen(t);
        if (s.compare(i, n, t) == 0)
        {
            i += n;
            return true;
        }
        return false;
    }
    bool peek(const char *t) { skip(); return s.compare(i, strlen(t), t) == 0; }
    static bool namechar(char c)
    {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
               c == '_' || c == '-' || c == '.' || c == ':' || (unsigned char)c >= 0x80;
    }
    std::string name()
    {
        skip();
        size_t b = i;
        if (i < s.size() && s[i] == '*')
            return s.substr(i++, 1);
        while (i < s.size() && namechar(s[i]) && !(s[i] == '.' && i == b))
            i++;
        return s.substr(b, i - b);
    }
    // the keyword w when it stands as a word here
    bool word(const char *w)
    {
        skip();
        size_t n = strlen(w);
        if (s.compare(i, n, w) == 0 && (i + n >= s.size() || !namechar(s[i + n])))
        {
            i += n;
            return true;
        }
        return false;
    }

    static xml_node_struct *doc_of(xml_node_struct *n)
    {
        while (n->parent)
            n = n->parent;
        return n;
    }

    static void add(std::vector<XNode>& out, XNode x)
    {
        for (auto& y: out)
            if (y.node == x.node && y.attr == x.attr)
                return;
        out.push_back(x);
    }

    static void descendants(xml_node_struct *n, bool self, std::vector<xml_node_struct*>& out)
    {
        if (self)
            out.push_back(n);
        for (auto *c = n->first_child; c; c = c->next)
            descendants(c, true, out);
    }

    std::vector<XNode> path(std::vector<XNode> ctx)
    {
        skip();
        bool deep = false;
        if (eat(".//"))
            deep = true;
        else if (eat("//"))
        {
            deep = true;
            ctx = {{doc_of(ctx[0].node), nullptr}};
        }
        else if (peek("/"))
        {
            eat("/");
            ctx = {{doc_of(ctx[0].node), nullptr}};
            skip();
            if (i >= s.size() || s[i] == ']' || s[i] == ')')
                return ctx;
        }
        else if (eat("./"))
            ;
        else if (peek(".") && !peek(".."))
        {
            eat(".");
            return ctx;
        }

        for (;;)
        {
            ctx = step(ctx, deep);
            if (!ok)
                return {};
            if (eat("//"))
                deep = true;
            else if (peek("/") )
            {
                eat("/");
                deep = false;
            }
            else
                break;
        }
        return ctx;
    }

    std::vector<XNode> step(const std::vector<XNode>& ctx, bool deep)
    {
        skip();
        bool attr = eat("@");
        if (!attr && eat(".."))
        {
            std::vector<XNode> out;
            for (auto& c: ctx)
                if (c.attr)
                    add(out, {c.node, nullptr});
                else if (c.node->parent)
                    add(out, {c.node->parent, nullptr});
            return out;
        }
        if (!attr && word("text"))
        {
            if (!eat("(") || !eat(")"))
            {
                ok = false;
                return {};
            }
            std::vector<XNode> out;
            for (auto& c: ctx)
            {
                std::vector<xml_node_struct*> all;
                if (deep)
                    descendants(c.node, false, all);
                else
                    for (auto *k = c.node->first_child; k; k = k->next)
                        all.push_back(k);
                for (auto *k: all)
                    if (k->type == node_pcdata || k->type == node_cdata)
                        add(out, {k, nullptr});
            }
            return out;
        }
        const std::string nm = name();
        if (nm.empty())
        {
            ok = false;
            return {};
        }

        std::vector<XNode> out;
        for (auto& c: ctx)
        {
            if (c.attr)
                continue;
            std::vector<xml_node_struct*> all;
            if (deep)
                descendants(c.node, !attr, all);
            else
                all.push_back(c.node);
            for (auto *n: all)
            {
                if (attr)
                {
                    if (n->type != node_element)
                        continue;
                    for (auto *a = n->first_attr; a; a = a->next)
                        if (nm == "*" || a->name == nm)
                            add(out, {n, a});
                    continue;
                }
                if (deep)
                {
                    // descendant-or-self then child: the children of each
                    if (n->type == node_element && n != c.node && (nm == "*" || n->name == nm))
                        add(out, {n, nullptr});
                    continue;
                }
                for (auto *k = n->first_child; k; k = k->next)
                    if (k->type == node_element && (nm == "*" || k->name == nm))
                        add(out, {k, nullptr});
            }
        }

        while (eat("["))
        {
            const size_t start = i;
            std::vector<XNode> kept;
            for (auto& x: out)
            {
                i = start;
                if (expr(x))
                    kept.push_back(x);
                if (!ok)
                    return {};
            }
            if (out.empty())
            {
                // still read the predicate through
                XNode none{doc_of(ctx[0].node), nullptr};
                i = start;
                expr(none);
            }
            if (!eat("]"))
            {
                ok = false;
                return {};
            }
            out.swap(kept);
        }
        return out;
    }

    static std::string string_value(const XNode& x)
    {
        if (x.attr)
            return x.attr->value;
        if (x.node->type != node_element && x.node->type != node_document)
            return x.node->value;
        std::string r;
        std::vector<xml_node_struct*> all;
        descendants(x.node, false, all);
        for (auto *n: all)
            if (n->type == node_pcdata || n->type == node_cdata)
                r += n->value;
        return r;
    }

    bool literal(std::string& out)
    {
        skip();
        if (i < s.size() && (s[i] == '\'' || s[i] == '"'))
        {
            char q = s[i++];
            size_t e = s.find(q, i);
            if (e == std::string::npos)
            {
                ok = false;
                return false;
            }
            out = s.substr(i, e - i);
            i = e + 1;
            return true;
        }
        return false;
    }

    bool expr(const XNode& ctx)
    {
        bool v = and_expr(ctx);
        while (word("or"))
            v = and_expr(ctx) || v;
        return v;
    }

    bool and_expr(const XNode& ctx)
    {
        bool v = unary(ctx);
        while (word("and"))
            v = unary(ctx) && v;
        return v;
    }

    bool unary(const XNode& ctx)
    {
        skip();
        if (word("not"))
        {
            if (!eat("("))
            {
                ok = false;
                return false;
            }
            bool v = expr(ctx);
            if (!eat(")"))
                ok = false;
            return !v;
        }
        if (eat("("))
        {
            bool v = expr(ctx);
            if (!eat(")"))
                ok = false;
            return v;
        }
        std::string lit;
        if (literal(lit))
        {
            // 'x' = path
            bool ne = eat("!=");
            if (!ne && !eat("="))
                return !lit.empty();
            auto nodes = path(std::vector<XNode>{ctx});
            for (auto& n: nodes)
                if ((string_value(n) == lit) != ne)
                    return true;
            return false;
        }
        auto nodes = path(std::vector<XNode>{ctx});
        if (!ok)
            return false;
        bool ne = false;
        if (eat("!="))
            ne = true;
        else if (!eat("="))
            return !nodes.empty();
        if (!literal(lit))
        {
            ok = false;
            return false;
        }
        for (auto& n: nodes)
            if ((string_value(n) == lit) != ne)
                return true;
        return false;
    }
};

} // anonymous namespace

// ---------------------------------------------------------------- xml_writer_stream

void xml_writer_stream::write(const void *data, size_t size)
{
    m_stream.write((const char*)data, (std::streamsize)size);
}

// ---------------------------------------------------------------- xml_attribute

const char_t *xml_attribute::name() const { return _attr ? sz(_attr->name) : ""; }
const char_t *xml_attribute::value() const { return _attr ? sz(_attr->value) : ""; }
const char_t *xml_attribute::as_string(const char_t *def) const { return _attr ? sz(_attr->value) : def; }
int xml_attribute::as_int(int def) const { return _attr ? (int)strtol(_attr->value.c_str(), nullptr, 0) : def; }
unsigned int xml_attribute::as_uint(unsigned int def) const
    { return _attr ? (unsigned)strtoul(_attr->value.c_str(), nullptr, 0) : def; }
double xml_attribute::as_double(double def) const { return _attr ? strtod(_attr->value.c_str(), nullptr) : def; }
bool xml_attribute::as_bool(bool def) const
{
    if (!_attr)
        return def;
    char c = _attr->value.empty() ? 0 : _attr->value[0];
    return c == '1' || c == 't' || c == 'T' || c == 'y' || c == 'Y';
}

bool xml_attribute::set_name(const char_t *rhs) { if (!_attr) return false; _attr->name = rhs ? rhs : ""; return true; }
bool xml_attribute::set_value(const char_t *rhs) { if (!_attr) return false; _attr->value = rhs ? rhs : ""; return true; }
bool xml_attribute::set_value(const char_t *rhs, size_t size)
    { if (!_attr) return false; _attr->value.assign(rhs, size); return true; }
bool xml_attribute::set_value(int rhs) { return set_value(std::to_string(rhs).c_str()); }
bool xml_attribute::set_value(unsigned int rhs) { return set_value(std::to_string(rhs).c_str()); }
bool xml_attribute::set_value(long rhs) { return set_value(std::to_string(rhs).c_str()); }
bool xml_attribute::set_value(unsigned long rhs) { return set_value(std::to_string(rhs).c_str()); }
bool xml_attribute::set_value(double rhs)
{
    char b[40];
    snprintf(b, sizeof(b), "%.17g", rhs);
    return set_value(b);
}
bool xml_attribute::set_value(bool rhs) { return set_value(rhs ? "true" : "false"); }

xml_attribute xml_attribute::next_attribute() const { return xml_attribute(_attr ? _attr->next : nullptr); }
xml_attribute xml_attribute::previous_attribute() const { return xml_attribute(_attr ? _attr->prev : nullptr); }

// ---------------------------------------------------------------- iterators

xml_node_iterator& xml_node_iterator::operator++()
{
    if (_wrap._root)
        _wrap._root = _wrap._root->next;
    return *this;
}

xml_node_iterator& xml_node_iterator::operator--()
{
    _wrap._root = _wrap._root ? _wrap._root->prev : (_parent ? _parent->last_child : nullptr);
    return *this;
}

xml_named_node_iterator& xml_named_node_iterator::operator++()
{
    if (_wrap._root)
    {
        auto *n = _wrap._root->next;
        while (n && !(n->type == node_element && n->name == _name))
            n = n->next;
        _wrap._root = n;
    }
    return *this;
}

xml_attribute_iterator& xml_attribute_iterator::operator++()
{
    if (_attr)
        _attr = _attr->next;
    return *this;
}

xml_attribute_iterator& xml_attribute_iterator::operator--()
{
    _attr = _attr ? _attr->prev : (_parent ? _parent->last_attr : nullptr);
    return *this;
}

// ---------------------------------------------------------------- xml_parse_result

const char *xml_parse_result::description() const
{
    switch (status)
    {
        case status_ok: return "No error";
        case status_file_not_found: return "File was not found";
        case status_io_error: return "Error reading from file/stream";
        case status_out_of_memory: return "Could not allocate memory";
        case status_internal_error: return "Internal error occurred";
        case status_unrecognized_tag: return "Could not determine tag type";
        case status_bad_pi: return "Error parsing document declaration/processing instruction";
        case status_bad_comment: return "Error parsing comment";
        case status_bad_cdata: return "Error parsing CDATA section";
        case status_bad_doctype: return "Error parsing document type declaration";
        case status_bad_pcdata: return "Error parsing PCDATA section";
        case status_bad_start_element: return "Error parsing start element tag";
        case status_bad_attribute: return "Error parsing element attribute";
        case status_bad_end_element: return "Error parsing end element tag";
        case status_end_element_mismatch: return "Start-end tags mismatch";
        case status_append_invalid_root: return "Unable to append nodes: root is not an element or document";
        case status_no_document_element: return "No document element found";
    }
    return "Unknown error";
}

// ---------------------------------------------------------------- xml_node

xml_node_type xml_node::type() const { return _root ? _root->type : node_null; }
const char_t *xml_node::name() const { return (_root && has_name(_root->type)) ? sz(_root->name) : ""; }
const char_t *xml_node::value() const { return (_root && has_value(_root->type)) ? sz(_root->value) : ""; }

xml_attribute xml_node::first_attribute() const { return xml_attribute(_root ? _root->first_attr : nullptr); }
xml_attribute xml_node::last_attribute() const { return xml_attribute(_root ? _root->last_attr : nullptr); }
xml_node xml_node::first_child() const { return xml_node(_root ? _root->first_child : nullptr); }
xml_node xml_node::last_child() const { return xml_node(_root ? _root->last_child : nullptr); }
xml_node xml_node::next_sibling() const { return xml_node(_root ? _root->next : nullptr); }
xml_node xml_node::previous_sibling() const { return xml_node(_root ? _root->prev : nullptr); }
xml_node xml_node::parent() const { return xml_node(_root ? _root->parent : nullptr); }

xml_node xml_node::next_sibling(const char_t *name) const
{
    for (auto *n = _root ? _root->next : nullptr; n; n = n->next)
        if (n->type == node_element && n->name == name)
            return xml_node(n);
    return xml_node();
}

xml_node xml_node::previous_sibling(const char_t *name) const
{
    for (auto *n = _root ? _root->prev : nullptr; n; n = n->prev)
        if (n->type == node_element && n->name == name)
            return xml_node(n);
    return xml_node();
}

xml_node xml_node::root() const
{
    if (!_root)
        return xml_node();
    auto *n = _root;
    while (n->parent)
        n = n->parent;
    return xml_node(n);
}

xml_text xml_node::text() const { return xml_text(_root); }

xml_node xml_node::child(const char_t *name) const
{
    for (auto *n = _root ? _root->first_child : nullptr; n; n = n->next)
        if (n->type == node_element && n->name == name)
            return xml_node(n);
    return xml_node();
}

xml_attribute xml_node::attribute(const char_t *name) const
{
    for (auto *a = _root ? _root->first_attr : nullptr; a; a = a->next)
        if (a->name == name)
            return xml_attribute(a);
    return xml_attribute();
}

const char_t *xml_node::child_value() const
{
    for (auto *n = _root ? _root->first_child : nullptr; n; n = n->next)
        if (n->type == node_pcdata || n->type == node_cdata)
            return sz(n->value);
    return "";
}

const char_t *xml_node::child_value(const char_t *name) const { return child(name).child_value(); }

bool xml_node::set_name(const char_t *rhs)
{
    if (!_root || !has_name(_root->type))
        return false;
    _root->name = rhs ? rhs : "";
    return true;
}

bool xml_node::set_value(const char_t *rhs)
{
    if (!_root || !has_value(_root->type))
        return false;
    _root->value = rhs ? rhs : "";
    return true;
}

xml_attribute xml_node::append_attribute(const char_t *name)
{
    if (!allows_attributes(_root))
        return xml_attribute();
    auto *a = new xml_attribute_struct;
    a->name = name ? name : "";
    attr_link_after(a, _root, _root->last_attr);
    return xml_attribute(a);
}

xml_attribute xml_node::prepend_attribute(const char_t *name)
{
    if (!allows_attributes(_root))
        return xml_attribute();
    auto *a = new xml_attribute_struct;
    a->name = name ? name : "";
    attr_link_after(a, _root, nullptr);
    return xml_attribute(a);
}

xml_attribute xml_node::insert_attribute_after(const char_t *name, const xml_attribute& attr)
{
    if (!allows_attributes(_root) || !owns_attr(_root, attr._attr))
        return xml_attribute();
    auto *a = new xml_attribute_struct;
    a->name = name ? name : "";
    attr_link_after(a, _root, attr._attr);
    return xml_attribute(a);
}

xml_attribute xml_node::insert_attribute_before(const char_t *name, const xml_attribute& attr)
{
    if (!allows_attributes(_root) || !owns_attr(_root, attr._attr))
        return xml_attribute();
    auto *a = new xml_attribute_struct;
    a->name = name ? name : "";
    attr_link_after(a, _root, attr._attr->prev);
    return xml_attribute(a);
}

bool xml_node::remove_attribute(const xml_attribute& a)
{
    if (!_root || !owns_attr(_root, a._attr))
        return false;
    attr_unlink(a._attr, _root);
    delete a._attr;
    return true;
}

bool xml_node::remove_attribute(const char_t *name) { return remove_attribute(attribute(name)); }

bool xml_node::remove_attributes()
{
    if (!_root)
        return false;
    while (_root->first_attr)
    {
        auto *a = _root->first_attr;
        attr_unlink(a, _root);
        delete a;
    }
    return true;
}

xml_node xml_node::append_child(xml_node_type type)
{
    if (!allows_child(_root, type))
        return xml_node();
    auto *n = new xml_node_struct(type);
    if (type == node_declaration)
        n->name = "xml";
    link_last(n, _root);
    return xml_node(n);
}

xml_node xml_node::prepend_child(xml_node_type type)
{
    if (!allows_child(_root, type))
        return xml_node();
    auto *n = new xml_node_struct(type);
    if (type == node_declaration)
        n->name = "xml";
    link_after(n, _root, nullptr);
    return xml_node(n);
}

xml_node xml_node::insert_child_after(xml_node_type type, const xml_node& node)
{
    if (!allows_child(_root, type) || !node._root || node._root->parent != _root)
        return xml_node();
    auto *n = new xml_node_struct(type);
    if (type == node_declaration)
        n->name = "xml";
    link_after(n, _root, node._root);
    return xml_node(n);
}

xml_node xml_node::insert_child_before(xml_node_type type, const xml_node& node)
{
    if (!allows_child(_root, type) || !node._root || node._root->parent != _root)
        return xml_node();
    auto *n = new xml_node_struct(type);
    if (type == node_declaration)
        n->name = "xml";
    link_after(n, _root, node._root->prev);
    return xml_node(n);
}

xml_node xml_node::append_child(const char_t *name)
{
    xml_node n = append_child(node_element);
    n.set_name(name);
    return n;
}

xml_node xml_node::prepend_child(const char_t *name)
{
    xml_node n = prepend_child(node_element);
    n.set_name(name);
    return n;
}

xml_node xml_node::insert_child_after(const char_t *name, const xml_node& node)
{
    xml_node n = insert_child_after(node_element, node);
    n.set_name(name);
    return n;
}

xml_node xml_node::insert_child_before(const char_t *name, const xml_node& node)
{
    xml_node n = insert_child_before(node_element, node);
    n.set_name(name);
    return n;
}

xml_node xml_node::append_copy(const xml_node& proto)
{
    if (!proto._root || !allows_child(_root, proto._root->type))
        return xml_node();
    auto *n = copy_tree(proto._root);
    link_last(n, _root);
    return xml_node(n);
}

xml_node xml_node::prepend_copy(const xml_node& proto)
{
    if (!proto._root || !allows_child(_root, proto._root->type))
        return xml_node();
    auto *n = copy_tree(proto._root);
    link_after(n, _root, nullptr);
    return xml_node(n);
}

xml_node xml_node::insert_copy_after(const xml_node& proto, const xml_node& node)
{
    if (!proto._root || !allows_child(_root, proto._root->type) || !node._root || node._root->parent != _root)
        return xml_node();
    auto *n = copy_tree(proto._root);
    link_after(n, _root, node._root);
    return xml_node(n);
}

xml_node xml_node::insert_copy_before(const xml_node& proto, const xml_node& node)
{
    if (!proto._root || !allows_child(_root, proto._root->type) || !node._root || node._root->parent != _root)
        return xml_node();
    auto *n = copy_tree(proto._root);
    link_after(n, _root, node._root->prev);
    return xml_node(n);
}

bool xml_node::remove_child(const xml_node& n)
{
    if (!_root || !n._root || n._root->parent != _root)
        return false;
    unlink(n._root);
    delete n._root;
    return true;
}

bool xml_node::remove_child(const char_t *name) { return remove_child(child(name)); }

bool xml_node::remove_children()
{
    if (!_root)
        return false;
    while (_root->first_child)
    {
        auto *c = _root->first_child;
        unlink(c);
        delete c;
    }
    return true;
}

xml_parse_result xml_node::append_buffer(const void *contents, size_t size, unsigned int options, xml_encoding encoding)
{
    xml_parse_result res;
    if (!allows_children(_root))
    {
        res.status = status_append_invalid_root;
        return res;
    }
    // pugixml takes a lone '&' (one that starts no reference) as text
    const char *d = (const char*)contents;
    std::string fixed;
    if ((options & parse_escapes) && memchr(d, '&', size))
    {
        for (size_t i = 0; i < size; i++)
        {
            if (d[i] != '&')
            {
                fixed += d[i];
                continue;
            }
            size_t j = i + 1;
            if (j < size && d[j] == '#')
                j++;
            const size_t name = j;
            while (j < size && (isalnum((unsigned char)d[j]) || d[j] == '_' || d[j] == ':' || d[j] == '.' || d[j] == '-'))
                j++;
            fixed += (j > name && j < size && d[j] == ';') ? "&" : "&amp;";
        }
        contents = fixed.data();
        size = fixed.size();
    }
    // the nodes read before an error stay, as pugixml leaves them
    xml_node_struct tmp(node_element);
    res = parse_into(&tmp, contents, size, options | parse_fragment, encoding, true);
    while (tmp.first_child)
    {
        auto *c = tmp.first_child;
        unlink(c);
        link_last(c, _root);
    }
    return res;
}

bool xml_tree_walker::begin(xml_node&) { return true; }
bool xml_tree_walker::end(xml_node&) { return true; }

bool xml_node::traverse(xml_tree_walker& walker)
{
    walker._depth = -1;
    xml_node arg_begin(_root);
    if (!walker.begin(arg_begin))
        return false;

    xml_node_struct *cur = _root ? _root->first_child : nullptr;
    if (cur)
    {
        ++walker._depth;
        do
        {
            xml_node arg_for_each(cur);
            if (!walker.for_each(arg_for_each))
                return false;
            if (cur->first_child)
            {
                ++walker._depth;
                cur = cur->first_child;
            }
            else if (cur->next)
                cur = cur->next;
            else
            {
                while (!cur->next && cur != _root && cur->parent)
                {
                    --walker._depth;
                    cur = cur->parent;
                }
                if (cur != _root)
                    cur = cur->next;
            }
        }
        while (cur && cur != _root);
    }

    walker._depth = -1;
    xml_node arg_end(_root);
    return walker.end(arg_end);
}

xpath_node xml_node::select_node(const char_t *query) const
{
    xpath_node_set s = select_nodes(query);
    return s.first();
}

xpath_node_set xml_node::select_nodes(const char_t *query) const
{
    if (!_root)
        return xpath_node_set();
    XPath xp(query);
    std::vector<xpath_node> v;
    for (auto& x: xp.run(_root))
        v.push_back(x.attr ? xpath_node(xml_attribute(x.attr), xml_node(x.node)) : xpath_node(xml_node(x.node)));
    return xpath_node_set(std::move(v));
}

void xml_node::print(xml_writer& writer, const char_t *indent, unsigned int flags, xml_encoding, unsigned int depth) const
{
    if (!_root)
        return;
    Out o(writer);
    write_tree(o, _root, indent, flags, depth);
}

void xml_node::print(std::ostream& os, const char_t *indent, unsigned int flags, xml_encoding encoding, unsigned int depth) const
{
    xml_writer_stream w(os);
    print(w, indent, flags, encoding, depth);
}

xml_node_iterator xml_node::begin() const { return xml_node_iterator(_root ? _root->first_child : nullptr, _root); }
xml_node_iterator xml_node::end() const { return xml_node_iterator(nullptr, _root); }
xml_attribute_iterator xml_node::attributes_begin() const
    { return xml_attribute_iterator(_root ? _root->first_attr : nullptr, _root); }
xml_attribute_iterator xml_node::attributes_end() const { return xml_attribute_iterator(nullptr, _root); }

xml_object_range<xml_node_iterator> xml_node::children() const
{
    return xml_object_range<xml_node_iterator>(begin(), end());
}

xml_object_range<xml_named_node_iterator> xml_node::children(const char_t *name) const
{
    xml_node first = child(name);
    return xml_object_range<xml_named_node_iterator>(xml_named_node_iterator(first._root, _root, name),
                                                     xml_named_node_iterator(nullptr, _root, name));
}

xml_object_range<xml_attribute_iterator> xml_node::attributes() const
{
    return xml_object_range<xml_attribute_iterator>(attributes_begin(), attributes_end());
}

// ---------------------------------------------------------------- xml_text

xml_node_struct *xml_text::data_node() const
{
    if (!_root)
        return nullptr;
    if (_root->type == node_pcdata || _root->type == node_cdata)
        return _root;
    for (auto *n = _root->first_child; n; n = n->next)
        if (n->type == node_pcdata || n->type == node_cdata)
            return n;
    return nullptr;
}

xml_node_struct *xml_text::data_new()
{
    if (auto *d = data_node())
        return d;
    return xml_node(_root).append_child(node_pcdata).internal_object();
}

bool xml_text::has_data() const { return data_node() != nullptr; }
const char_t *xml_text::get() const { auto *d = data_node(); return d ? sz(d->value) : ""; }
const char_t *xml_text::as_string(const char_t *def) const { auto *d = data_node(); return d ? sz(d->value) : def; }
int xml_text::as_int(int def) const { auto *d = data_node(); return d ? (int)strtol(d->value.c_str(), nullptr, 0) : def; }
double xml_text::as_double(double def) const { auto *d = data_node(); return d ? strtod(d->value.c_str(), nullptr) : def; }
bool xml_text::as_bool(bool def) const
{
    auto *d = data_node();
    if (!d)
        return def;
    char c = d->value.empty() ? 0 : d->value[0];
    return c == '1' || c == 't' || c == 'T' || c == 'y' || c == 'Y';
}

bool xml_text::set(const char_t *rhs)
{
    auto *d = data_new();
    if (!d)
        return false;
    d->value = rhs ? rhs : "";
    return true;
}

bool xml_text::set(const char_t *rhs, size_t size)
{
    auto *d = data_new();
    if (!d)
        return false;
    d->value.assign(rhs, size);
    return true;
}

bool xml_text::set(int rhs) { return set(std::to_string(rhs).c_str()); }
bool xml_text::set(unsigned int rhs) { return set(std::to_string(rhs).c_str()); }
bool xml_text::set(long rhs) { return set(std::to_string(rhs).c_str()); }
bool xml_text::set(double rhs)
{
    char b[40];
    snprintf(b, sizeof(b), "%.17g", rhs);
    return set(b);
}
bool xml_text::set(bool rhs) { return set(rhs ? "true" : "false"); }

xml_node xml_text::data() const { return xml_node(data_node()); }

// ---------------------------------------------------------------- xml_document

xml_document::xml_document() { _root = new xml_node_struct(node_document); }
xml_document::~xml_document() { delete _root; }

xml_document::xml_document(xml_document&& rhs) noexcept
{
    _root = rhs._root;
    rhs._root = new xml_node_struct(node_document);
}

xml_document& xml_document::operator=(xml_document&& rhs) noexcept
{
    if (this != &rhs)
    {
        delete _root;
        _root = rhs._root;
        rhs._root = new xml_node_struct(node_document);
    }
    return *this;
}

void xml_document::reset()
{
    delete _root;
    _root = new xml_node_struct(node_document);
}

void xml_document::reset(const xml_document& proto)
{
    reset();
    for (auto *c = proto._root->first_child; c; c = c->next)
        link_last(copy_tree(c), _root);
}

xml_parse_result xml_document::load_buffer(const void *contents, size_t size, unsigned int options, xml_encoding encoding)
{
    reset();
    return parse_into(_root, contents, size, options, encoding, false);
}

xml_parse_result xml_document::load_string(const char_t *contents, unsigned int options)
{
    return load_buffer(contents, strlen(contents), options, encoding_utf8);
}

xml_parse_result xml_document::load(std::istream& stream, unsigned int options, xml_encoding encoding)
{
    std::ostringstream s;
    s << stream.rdbuf();
    if (stream.bad())
    {
        reset();
        xml_parse_result r;
        r.status = status_io_error;
        return r;
    }
    const std::string data = s.str();
    return load_buffer(data.data(), data.size(), options, encoding);
}

xml_parse_result xml_document::load_file(const char *path, unsigned int options, xml_encoding encoding)
{
    reset();
    xml_parse_result r;
    FILE *f = fopen(path, "rb");
    if (!f)
    {
        r.status = status_file_not_found;
        return r;
    }
    std::string data;
    char buf[16384];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        data.append(buf, n);
    const bool err = ferror(f) != 0;
    fclose(f);
    if (err)
    {
        r.status = status_io_error;
        return r;
    }
    return load_buffer(data.data(), data.size(), options, encoding);
}

void xml_document::save(xml_writer& writer, const char_t *indent, unsigned int flags, xml_encoding) const
{
    Out o(writer);
    if (flags & format_write_bom)
        o.put("\xEF\xBB\xBF");
    bool hasDecl = false;
    for (auto *c = _root->first_child; c; c = c->next)
        if (c->type == node_declaration)
            hasDecl = true;
    if (!(flags & format_no_declaration) && !hasDecl)
    {
        o.put("<?xml version=\"1.0\"?>");
        if (!(flags & format_raw))
            o.put('\n');
    }
    write_tree(o, _root, indent, flags, 0);
}

void xml_document::save(std::ostream& stream, const char_t *indent, unsigned int flags, xml_encoding encoding) const
{
    xml_writer_stream w(stream);
    save(w, indent, flags, encoding);
}

bool xml_document::save_file(const char *path, const char_t *indent, unsigned int flags, xml_encoding encoding) const
{
    FILE *f = fopen(path, (flags & format_save_file_text) ? "w" : "wb");
    if (!f)
        return false;
    string_writer w;
    save(w, indent, flags, encoding);
    const bool ok = fwrite(w.s.data(), 1, w.s.size(), f) == w.s.size();
    return fclose(f) == 0 && ok;
}

xml_node xml_document::document_element() const
{
    for (auto *c = _root->first_child; c; c = c->next)
        if (c->type == node_element)
            return xml_node(c);
    return xml_node();
}

// ---------------------------------------------------------------- UTF-8 <-> wide

std::string as_utf8(const wchar_t *str)
{
    std::string out;
    if (!str)
        return out;
    for (const wchar_t *p = str; *p; p++)
    {
        unsigned long c = (unsigned long)*p;
        if (sizeof(wchar_t) == 2 && c >= 0xD800 && c <= 0xDBFF && p[1] >= 0xDC00 && p[1] <= 0xDFFF)
        {
            c = 0x10000 + ((c - 0xD800) << 10) + ((unsigned long)p[1] - 0xDC00);
            p++;
        }
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
    return out;
}

std::string as_utf8(const std::wstring& str) { return as_utf8(str.c_str()); }

std::wstring as_wide(const char *str)
{
    std::wstring out;
    if (!str)
        return out;
    const unsigned char *p = (const unsigned char*)str;
    while (*p)
    {
        unsigned long c = *p++;
        int n = 0;
        if (c >= 0xF0) { n = 3; c &= 0x07; }
        else if (c >= 0xE0) { n = 2; c &= 0x0F; }
        else if (c >= 0xC0) { n = 1; c &= 0x1F; }
        for (int k = 0; k < n && (*p & 0xC0) == 0x80; k++)
            c = (c << 6) | (*p++ & 0x3F);
        if (sizeof(wchar_t) == 2 && c >= 0x10000)
        {
            c -= 0x10000;
            out += (wchar_t)(0xD800 + (c >> 10));
            out += (wchar_t)(0xDC00 + (c & 0x3FF));
        }
        else
            out += (wchar_t)c;
    }
    return out;
}

std::wstring as_wide(const std::string& str) { return as_wide(str.c_str()); }

} // namespace pugi
