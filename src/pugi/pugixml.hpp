/*
 *  The pugixml calls Poedit uses, on our own XML parser (OXP, GFOpenXML).
 *  Poedit's XLIFF, Qt Linguist, RESX, xcloc and TMX code includes
 *  <pugixml.hpp> (HAVE_PUGIXML) and runs unchanged on this.
 *
 *  Our own code: the names follow pugixml's interface so the callers keep
 *  their source; the tree, the writer and the small XPath are written here,
 *  and parsing is OXP's.
 *
 *  XPath: location paths with child (/) and descendant (//) steps, element
 *  names or *, @attributes, and predicates made of paths, comparisons of a
 *  path with a string (= and !=), and, or, not() and brackets. That is what
 *  Poedit asks for.
 *
 *  Copyright (c) 2026 AmiCradle Team
 */

#ifndef GF_PUGIXML_HPP
#define GF_PUGIXML_HPP

#include <cstddef>
#include <iosfwd>
#include <iterator>
#include <string>
#include <vector>

#define PUGIXML_VERSION 1150
#define PUGIXML_TEXT(t) t
#define PUGIXML_CHAR char

namespace pugi
{
typedef char char_t;
typedef std::string string_t;

enum xml_node_type
{
    node_null, node_document, node_element, node_pcdata, node_cdata,
    node_comment, node_pi, node_declaration, node_doctype
};

const unsigned int parse_minimal = 0x0000;
const unsigned int parse_pi = 0x0001;
const unsigned int parse_comments = 0x0002;
const unsigned int parse_cdata = 0x0004;
const unsigned int parse_ws_pcdata = 0x0008;
const unsigned int parse_escapes = 0x0010;
const unsigned int parse_eol = 0x0020;
const unsigned int parse_wconv_attribute = 0x0040;
const unsigned int parse_wnorm_attribute = 0x0080;
const unsigned int parse_declaration = 0x0100;
const unsigned int parse_doctype = 0x0200;
const unsigned int parse_ws_pcdata_single = 0x0400;
const unsigned int parse_trim_pcdata = 0x0800;
const unsigned int parse_fragment = 0x1000;
const unsigned int parse_embed_pcdata = 0x2000;
const unsigned int parse_merge_pcdata = 0x4000;
const unsigned int parse_default = parse_cdata | parse_escapes | parse_wconv_attribute | parse_eol;
const unsigned int parse_full = parse_default | parse_pi | parse_comments | parse_declaration | parse_doctype;

enum xml_encoding
{
    encoding_auto, encoding_utf8, encoding_utf16_le, encoding_utf16_be, encoding_utf16,
    encoding_utf32_le, encoding_utf32_be, encoding_utf32, encoding_wchar, encoding_latin1
};

const unsigned int format_indent = 0x01;
const unsigned int format_write_bom = 0x02;
const unsigned int format_raw = 0x04;
const unsigned int format_no_declaration = 0x08;
const unsigned int format_no_escapes = 0x10;
const unsigned int format_save_file_text = 0x20;
const unsigned int format_indent_attributes = 0x40;
const unsigned int format_no_empty_element_tags = 0x80;
const unsigned int format_skip_control_chars = 0x100;
const unsigned int format_attribute_single_quote = 0x200;
const unsigned int format_default = format_indent;

struct xml_attribute_struct;
struct xml_node_struct;
class xml_node;
class xml_node_iterator;
class xml_named_node_iterator;
class xml_text;
class xpath_node;
class xpath_node_set;
class xml_tree_walker;

class xml_writer
{
public:
    virtual ~xml_writer() {}
    virtual void write(const void *data, size_t size) = 0;
};

class xml_writer_stream : public xml_writer
{
public:
    xml_writer_stream(std::ostream& stream) : m_stream(stream) {}
    void write(const void *data, size_t size) override;
private:
    std::ostream& m_stream;
};

template <typename It> class xml_object_range
{
public:
    typedef It const_iterator;
    typedef It iterator;
    xml_object_range(It b, It e) : _begin(b), _end(e) {}
    It begin() const { return _begin; }
    It end() const { return _end; }
    bool empty() const { return _begin == _end; }
private:
    It _begin, _end;
};

class xml_attribute
{
    friend class xml_node;
    friend class xml_attribute_iterator;
    friend class xpath_node;
public:
    xml_attribute() : _attr(nullptr) {}
    explicit xml_attribute(xml_attribute_struct *a) : _attr(a) {}

    explicit operator bool() const { return _attr != nullptr; }
    bool operator!() const { return !_attr; }
    bool operator==(const xml_attribute& r) const { return _attr == r._attr; }
    bool operator!=(const xml_attribute& r) const { return _attr != r._attr; }
    bool empty() const { return !_attr; }

    const char_t *name() const;
    const char_t *value() const;
    const char_t *as_string(const char_t *def = "") const;
    int as_int(int def = 0) const;
    unsigned int as_uint(unsigned int def = 0) const;
    double as_double(double def = 0) const;
    bool as_bool(bool def = false) const;

    bool set_name(const char_t *rhs);
    bool set_value(const char_t *rhs);
    bool set_value(const char_t *rhs, size_t size);
    bool set_value(int rhs);
    bool set_value(unsigned int rhs);
    bool set_value(long rhs);
    bool set_value(unsigned long rhs);
    bool set_value(double rhs);
    bool set_value(bool rhs);

    xml_attribute& operator=(const char_t *rhs) { set_value(rhs); return *this; }
    xml_attribute& operator=(int rhs) { set_value(rhs); return *this; }
    xml_attribute& operator=(unsigned int rhs) { set_value(rhs); return *this; }
    xml_attribute& operator=(long rhs) { set_value(rhs); return *this; }
    xml_attribute& operator=(unsigned long rhs) { set_value(rhs); return *this; }
    xml_attribute& operator=(double rhs) { set_value(rhs); return *this; }
    xml_attribute& operator=(bool rhs) { set_value(rhs); return *this; }

    xml_attribute next_attribute() const;
    xml_attribute previous_attribute() const;

    xml_attribute_struct *internal_object() const { return _attr; }
private:
    xml_attribute_struct *_attr;
};

class xml_attribute_iterator
{
public:
    typedef ptrdiff_t difference_type;
    typedef xml_attribute value_type;
    typedef xml_attribute *pointer;
    typedef xml_attribute& reference;
    typedef std::bidirectional_iterator_tag iterator_category;

    xml_attribute_iterator() : _attr(nullptr), _parent(nullptr) {}
    xml_attribute_iterator(xml_attribute_struct *a, xml_node_struct *parent) : _attr(a), _parent(parent) {}
    bool operator==(const xml_attribute_iterator& r) const { return _attr == r._attr && _parent == r._parent; }
    bool operator!=(const xml_attribute_iterator& r) const { return !(*this == r); }
    xml_attribute& operator*() const { _wrap = xml_attribute(_attr); return _wrap; }
    xml_attribute *operator->() const { _wrap = xml_attribute(_attr); return &_wrap; }
    xml_attribute_iterator& operator++();
    xml_attribute_iterator operator++(int) { auto t = *this; ++*this; return t; }
    xml_attribute_iterator& operator--();
    xml_attribute_iterator operator--(int) { auto t = *this; --*this; return t; }
private:
    xml_attribute_struct *_attr;
    xml_node_struct *_parent;
    mutable xml_attribute _wrap;
};

enum xml_parse_status
{
    status_ok, status_file_not_found, status_io_error, status_out_of_memory, status_internal_error,
    status_unrecognized_tag, status_bad_pi, status_bad_comment, status_bad_cdata, status_bad_doctype,
    status_bad_pcdata, status_bad_start_element, status_bad_attribute, status_bad_end_element,
    status_end_element_mismatch, status_append_invalid_root, status_no_document_element
};

struct xml_parse_result
{
    xml_parse_status status;
    ptrdiff_t offset;
    xml_encoding encoding;

    xml_parse_result() : status(status_internal_error), offset(0), encoding(encoding_auto) {}
    operator bool() const { return status == status_ok; }
    const char *description() const;
};

class xml_node
{
    friend class xml_document;
    friend class xml_text;
    friend class xpath_node;
    friend class xml_node_iterator;
    friend class xml_named_node_iterator;
public:
    typedef xml_attribute_iterator attribute_iterator;

    xml_node() : _root(nullptr) {}
    explicit xml_node(xml_node_struct *p) : _root(p) {}

    explicit operator bool() const { return _root != nullptr; }
    bool operator!() const { return !_root; }
    bool operator==(const xml_node& r) const { return _root == r._root; }
    bool operator!=(const xml_node& r) const { return _root != r._root; }
    bool operator<(const xml_node& r) const { return _root < r._root; }
    bool empty() const { return !_root; }

    xml_node_type type() const;
    const char_t *name() const;
    const char_t *value() const;

    xml_attribute first_attribute() const;
    xml_attribute last_attribute() const;
    xml_node first_child() const;
    xml_node last_child() const;
    xml_node next_sibling() const;
    xml_node previous_sibling() const;
    xml_node next_sibling(const char_t *name) const;
    xml_node previous_sibling(const char_t *name) const;
    xml_node parent() const;
    xml_node root() const;
    xml_text text() const;
    xml_node child(const char_t *name) const;
    xml_attribute attribute(const char_t *name) const;
    const char_t *child_value() const;
    const char_t *child_value(const char_t *name) const;

    bool set_name(const char_t *rhs);
    bool set_value(const char_t *rhs);

    xml_attribute append_attribute(const char_t *name);
    xml_attribute prepend_attribute(const char_t *name);
    xml_attribute insert_attribute_after(const char_t *name, const xml_attribute& attr);
    xml_attribute insert_attribute_before(const char_t *name, const xml_attribute& attr);
    bool remove_attribute(const xml_attribute& a);
    bool remove_attribute(const char_t *name);
    bool remove_attributes();

    xml_node append_child(xml_node_type type = node_element);
    xml_node prepend_child(xml_node_type type = node_element);
    xml_node insert_child_after(xml_node_type type, const xml_node& node);
    xml_node insert_child_before(xml_node_type type, const xml_node& node);
    xml_node append_child(const char_t *name);
    xml_node prepend_child(const char_t *name);
    xml_node insert_child_after(const char_t *name, const xml_node& node);
    xml_node insert_child_before(const char_t *name, const xml_node& node);
    xml_node append_copy(const xml_node& proto);
    xml_node prepend_copy(const xml_node& proto);
    xml_node insert_copy_after(const xml_node& proto, const xml_node& node);
    xml_node insert_copy_before(const xml_node& proto, const xml_node& node);
    bool remove_child(const xml_node& n);
    bool remove_child(const char_t *name);
    bool remove_children();

    // parses contents as a fragment and appends its nodes to this one
    xml_parse_result append_buffer(const void *contents, size_t size,
                                   unsigned int options = parse_default, xml_encoding encoding = encoding_auto);

    template <typename Predicate> xml_attribute find_attribute(Predicate pred) const
    {
        for (xml_attribute a = first_attribute(); a; a = a.next_attribute())
            if (pred(a))
                return a;
        return xml_attribute();
    }
    template <typename Predicate> xml_node find_child(Predicate pred) const
    {
        for (xml_node n = first_child(); n; n = n.next_sibling())
            if (pred(n))
                return n;
        return xml_node();
    }
    template <typename Predicate> xml_node find_node(Predicate pred) const
    {
        if (!_root)
            return xml_node();
        xml_node cur = first_child();
        while (cur && cur._root != _root)
        {
            if (pred(cur))
                return cur;
            if (cur.first_child())
                cur = cur.first_child();
            else if (cur.next_sibling())
                cur = cur.next_sibling();
            else
            {
                while (!cur.next_sibling() && cur._root != _root)
                    cur = cur.parent();
                if (cur._root != _root)
                    cur = cur.next_sibling();
            }
        }
        return xml_node();
    }

    bool traverse(xml_tree_walker& walker);

    xpath_node select_node(const char_t *query) const;
    xpath_node_set select_nodes(const char_t *query) const;

    void print(xml_writer& writer, const char_t *indent = "\t", unsigned int flags = format_default,
               xml_encoding encoding = encoding_auto, unsigned int depth = 0) const;
    void print(std::ostream& os, const char_t *indent = "\t", unsigned int flags = format_default,
               xml_encoding encoding = encoding_auto, unsigned int depth = 0) const;

    xml_node_iterator begin() const;
    xml_node_iterator end() const;
    xml_attribute_iterator attributes_begin() const;
    xml_attribute_iterator attributes_end() const;
    xml_object_range<xml_node_iterator> children() const;
    xml_object_range<xml_named_node_iterator> children(const char_t *name) const;
    xml_object_range<xml_attribute_iterator> attributes() const;

    ptrdiff_t offset_debug() const { return -1; }
    size_t hash_value() const { return (size_t)_root; }
    xml_node_struct *internal_object() const { return _root; }

protected:
    xml_node_struct *_root;
};

class xml_node_iterator
{
public:
    typedef ptrdiff_t difference_type;
    typedef xml_node value_type;
    typedef xml_node *pointer;
    typedef xml_node& reference;
    typedef std::bidirectional_iterator_tag iterator_category;

    xml_node_iterator() : _parent(nullptr) {}
    xml_node_iterator(xml_node_struct *n, xml_node_struct *parent) : _wrap(n), _parent(parent) {}
    bool operator==(const xml_node_iterator& r) const { return _wrap == r._wrap && _parent == r._parent; }
    bool operator!=(const xml_node_iterator& r) const { return !(*this == r); }
    xml_node& operator*() const { return _wrap; }
    xml_node *operator->() const { return &_wrap; }
    xml_node_iterator& operator++();
    xml_node_iterator operator++(int) { auto t = *this; ++*this; return t; }
    xml_node_iterator& operator--();
    xml_node_iterator operator--(int) { auto t = *this; --*this; return t; }
private:
    mutable xml_node _wrap;
    xml_node_struct *_parent;
};

class xml_named_node_iterator
{
public:
    typedef ptrdiff_t difference_type;
    typedef xml_node value_type;
    typedef xml_node *pointer;
    typedef xml_node& reference;
    typedef std::forward_iterator_tag iterator_category;

    xml_named_node_iterator() : _parent(nullptr) {}
    xml_named_node_iterator(xml_node_struct *n, xml_node_struct *parent, const char_t *name)
        : _wrap(n), _parent(parent), _name(name ? name : "") {}
    bool operator==(const xml_named_node_iterator& r) const { return _wrap == r._wrap; }
    bool operator!=(const xml_named_node_iterator& r) const { return _wrap != r._wrap; }
    xml_node& operator*() const { return _wrap; }
    xml_node *operator->() const { return &_wrap; }
    xml_named_node_iterator& operator++();
    xml_named_node_iterator operator++(int) { auto t = *this; ++*this; return t; }
private:
    mutable xml_node _wrap;
    xml_node_struct *_parent;
    std::string _name;
};

class xml_text
{
    friend class xml_node;
public:
    xml_text() : _root(nullptr) {}
    explicit operator bool() const;
    bool operator!() const { return !(bool)*this; }
    bool empty() const { return !(bool)*this; }

    const char_t *get() const;
    const char_t *as_string(const char_t *def = "") const;
    int as_int(int def = 0) const;
    double as_double(double def = 0) const;
    bool as_bool(bool def = false) const;

    bool set(const char_t *rhs);
    bool set(const char_t *rhs, size_t size);
    bool set(int rhs);
    bool set(unsigned int rhs);
    bool set(long rhs);
    bool set(double rhs);
    bool set(bool rhs);

    xml_text& operator=(const char_t *rhs) { set(rhs); return *this; }
    xml_text& operator=(int rhs) { set(rhs); return *this; }
    xml_text& operator=(unsigned int rhs) { set(rhs); return *this; }
    xml_text& operator=(long rhs) { set(rhs); return *this; }
    xml_text& operator=(double rhs) { set(rhs); return *this; }
    xml_text& operator=(bool rhs) { set(rhs); return *this; }

    xml_node data() const;
private:
    explicit xml_text(xml_node_struct *root) : _root(root) {}
    xml_node_struct *data_node() const;
    xml_node_struct *data_new();
    xml_node_struct *_root;
};

class xml_tree_walker
{
    friend class xml_node;
public:
    xml_tree_walker() : _depth(0) {}
    virtual ~xml_tree_walker() {}
    virtual bool begin(xml_node& node);
    virtual bool for_each(xml_node& node) = 0;
    virtual bool end(xml_node& node);
protected:
    int depth() const { return _depth; }
private:
    int _depth;
};

class xml_document : public xml_node
{
public:
    xml_document();
    ~xml_document();
    xml_document(xml_document&& rhs) noexcept;
    xml_document& operator=(xml_document&& rhs) noexcept;
    xml_document(const xml_document&) = delete;
    xml_document& operator=(const xml_document&) = delete;

    void reset();
    void reset(const xml_document& proto);

    xml_parse_result load(std::istream& stream, unsigned int options = parse_default, xml_encoding encoding = encoding_auto);
    xml_parse_result load_string(const char_t *contents, unsigned int options = parse_default);
    xml_parse_result load_file(const char *path, unsigned int options = parse_default, xml_encoding encoding = encoding_auto);
    xml_parse_result load_buffer(const void *contents, size_t size, unsigned int options = parse_default,
                                 xml_encoding encoding = encoding_auto);

    void save(xml_writer& writer, const char_t *indent = "\t", unsigned int flags = format_default,
              xml_encoding encoding = encoding_auto) const;
    void save(std::ostream& stream, const char_t *indent = "\t", unsigned int flags = format_default,
              xml_encoding encoding = encoding_auto) const;
    bool save_file(const char *path, const char_t *indent = "\t", unsigned int flags = format_default,
                   xml_encoding encoding = encoding_auto) const;

    xml_node document_element() const;
};

// ---- XPath (the subset described at the top) ----

class xpath_node
{
public:
    xpath_node() {}
    xpath_node(const xml_node& node) : _node(node) {}
    xpath_node(const xml_attribute& attribute, const xml_node& parent) : _node(parent), _attribute(attribute) {}

    xml_node node() const { return _attribute ? xml_node() : _node; }
    xml_attribute attribute() const { return _attribute; }
    xml_node parent() const { return _attribute ? _node : _node.parent(); }

    explicit operator bool() const { return _node || _attribute; }
    bool operator!() const { return !(bool)*this; }
    bool operator==(const xpath_node& n) const { return _node == n._node && _attribute == n._attribute; }
    bool operator!=(const xpath_node& n) const { return !(*this == n); }
private:
    xml_node _node;
    xml_attribute _attribute;
};

class xpath_node_set
{
public:
    enum type_t { type_unsorted, type_sorted, type_sorted_reverse };
    typedef const xpath_node *const_iterator;
    typedef const xpath_node *iterator;

    xpath_node_set() {}
    xpath_node_set(std::vector<xpath_node>&& v) : _nodes(std::move(v)) {}

    type_t type() const { return type_sorted; }
    size_t size() const { return _nodes.size(); }
    bool empty() const { return _nodes.empty(); }
    const xpath_node& operator[](size_t i) const { return _nodes[i]; }
    const_iterator begin() const { return _nodes.data(); }
    const_iterator end() const { return _nodes.data() + _nodes.size(); }
    xpath_node first() const { return _nodes.empty() ? xpath_node() : _nodes.front(); }
    void sort(bool = false) {}
private:
    std::vector<xpath_node> _nodes;
};

// UTF-8 <-> wide strings
std::string as_utf8(const wchar_t *str);
std::string as_utf8(const std::wstring& str);
std::wstring as_wide(const char *str);
std::wstring as_wide(const std::string& str);

} // namespace pugi

#endif // GF_PUGIXML_HPP
