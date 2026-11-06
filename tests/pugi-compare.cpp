#include <pugixml.hpp>
#include <sstream>
#include <iostream>
#include <iterator>
#include <cstring>
using namespace pugi;
constexpr auto F = parse_full | parse_ws_pcdata | parse_fragment;
static void out(const char* tag, const std::string& s){ std::cout << "== " << tag << "\n" << s << "\n"; }
static std::string save(xml_document& d, const char* ind, unsigned fl){ std::ostringstream s; d.save(s, ind, fl); return s.str(); }
static std::string pr(xml_node n, unsigned fl=format_raw){ std::ostringstream s; n.print(s, "", fl); return s.str(); }
struct W : xml_tree_walker { std::string log; bool begin(xml_node& n) override { log += std::string("B:")+n.name()+" "; return true; }
  bool for_each(xml_node& n) override { log += std::to_string(depth()) + ":" + (n.type()==node_element? n.name() : "#") + " "; return true; }
  bool end(xml_node& n) override { log += "E"; return true; } };
int main(){
 const char* ts =
 "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n<!DOCTYPE TS>\n<TS version=\"2.1\" language=\"cs_CZ\">\n"
 "<context>\n    <name>Main</name>\n    <message>\n        <location filename=\"a.cpp\" line=\"3\"/>\n"
 "        <source>Open &amp; save &lt;file&gt; &quot;x&quot;</source>\n        <translation type=\"unfinished\"></translation>\n    </message>\n"
 "    <message>\n        <source>Old</source>\n        <translation type=\"vanished\">Stare</translation>\n    </message>\n"
 "    <message>\n        <source>Gone</source>\n        <translation type=\"obsolete\">Pryc</translation>\n    </message>\n"
 "    <!-- a comment -->\n    <message numerus=\"yes\"><source>%n file(s)</source><translation><numerusform>a</numerusform><numerusform></numerusform></translation></message>\n"
 "</context>\n</TS>\n";
 { xml_document d; auto r = d.load_buffer(ts, strlen(ts), F); out("ts load", std::string(r.description()) + " root=" + d.child("TS").attribute("language").value());
   out("ts raw roundtrip", save(d, "\t", format_raw | format_no_empty_element_tags));
   auto msg = d.child("TS").child("context").child("message");
   auto tr = msg.child("translation"); tr.remove_attribute("type"); tr.text() = "Otevřít & uložit <soubor>";
   msg.insert_child_after("translatorcomment", msg.child("source")).text() = "c\"q\"";
   for (auto& x: d.select_nodes("//message[translation[@type='vanished' or @type='obsolete']]")) {
     auto node = x.node(); auto parent = x.parent(); auto sib = node.previous_sibling();
     if (sib && sib.type()==node_pcdata) parent.remove_child(sib); parent.remove_child(node); }
   msg.remove_child("translatorcomment"); msg.append_child("extra").append_attribute("k") = "v\tw\nx";
   out("ts edited", save(d, "\t", format_raw | format_no_empty_element_tags));
   out("ts edited indent", save(d, "  ", format_default));
   out("ts no flags", save(d, "\t", 0));
   std::cout << "count messages " << std::distance(d.child("TS").child("context").children("message").begin(), d.child("TS").child("context").children("message").end()) << "\n";
 }
 const char* xl =
 "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<xliff xmlns=\"urn:oasis:names:tc:xliff:document:1.2\" version=\"1.2\">\n"
 "  <file original=\"x\" source-language=\"en\" target-language=\"de\" datatype=\"plaintext\">\n    <body>\n"
 "      <trans-unit id=\"1\">\n        <source>Hello <g id=\"1\">bold</g> <x id=\"2\"/> &amp; <![CDATA[<raw>]]></source>\n"
 "        <note category=\"location\">a.c:1</note><note>dev note</note>\n"
 "        <context-group purpose=\"location\"><context context-type=\"sourcefile\">a.c</context></context-group>\n      </trans-unit>\n"
 "      <group><trans-unit id=\"2\"><source>Plain</source><target state=\"translated\">Schlicht</target></trans-unit></group>\n"
 "    </body>\n  </file>\n</xliff>";
 { xml_document d; auto r = d.load_buffer(xl, strlen(xl), F); out("xl load", r.description());
   auto file = d.child("xliff").child("file");
   for (auto u: file.select_nodes(".//trans-unit")) { auto n = u.node(); std::cout << "unit " << n.attribute("id").value() << "\n";
     std::string m; for (auto c: n.child("source").children()) m += pr(c); out("markup", m);
     for (auto no: n.select_nodes(".//note[not(@category='location')]")) out("note", no.node().text().get());
     for (auto no: n.select_nodes(".//note[@category='location']")) out("loc", no.node().text().get());
     for (auto cg: n.select_nodes(".//context-group[@purpose='location']")) out("cg", pr(cg.node()));
     W w; n.child("source").traverse(w); out("walk", w.log);
   }
   auto u1 = file.child("body").child("trans-unit");
   auto ws_after = u1.child("source").next_sibling();
   auto tg = u1.insert_child_after("target", u1.child("source"));
   u1.insert_child_after(node_pcdata, u1.child("source")).text() = ws_after.text().get();
   std::string s = "Hallo <g id=\"1\">fett</g> <x id=\"2\"/> &amp; mehr";
   auto ar = tg.append_buffer(s.c_str(), s.size(), F, encoding_utf8); out("append", ar.description());
   std::string s2 = "just text & no";
   xml_node t2 = u1.append_child("t2"); auto ar2 = t2.append_buffer(s2.c_str(), s2.size(), F, encoding_utf8); out("append2", std::string(ar2.description()) + " status " + std::to_string(ar2.status));
   std::string s3 = "plain text";
   xml_node t3 = u1.append_child("t3"); auto ar3 = t3.append_buffer(s3.c_str(), s3.size(), F, encoding_utf8); out("append3", std::string(ar3.description()) + " status " + std::to_string(ar3.status));
   std::string s4 = "bad <g>x</h>";
   xml_node t4 = u1.append_child("t4"); auto ar4 = t4.append_buffer(s4.c_str(), s4.size(), F, encoding_utf8); out("append4", std::string(" status ") + std::to_string(ar4.status) + " kids " + (t4.first_child()?"y":"n"));
   out("xl saved", save(d, "\t", format_raw));
   out("xpath segment none", std::to_string(d.select_nodes(".//segment").size()));
   out("xpath attr", d.child("xliff").select_node("file/@target-language").attribute().value());
 }
 { xml_document d; auto root = d.append_child("tmx"); root.append_attribute("version") = "1.4";
   auto h = root.append_child("header"); h.append_attribute("srclang") = "en"; auto b = root.append_child("body");
   auto tu = b.append_child("tu"); tu.append_attribute("creationdate") = "20260101T000000Z";
   auto tuv = tu.append_child("tuv"); tuv.append_attribute("xml:lang") = "en"; tuv.append_child("seg").text() = as_utf8(L"Open <file>").c_str();
   tuv = tu.append_child("tuv"); tuv.append_attribute("xml:lang") = "cs"; tuv.append_child("seg").text() = as_utf8(L"Otevřít").c_str();
   b.append_child("tu");
   std::ostringstream o; d.save(o); out("tmx", o.str());
   xml_document e; std::istringstream in(o.str()); auto r = e.load(in); out("tmx reload", r.description());
   for (auto t: e.child("tmx").child("body").children("tu")) for (auto v: t.children("tuv")) { std::wstring w; for (auto c: v.child("seg").children()) if (c.type()==node_pcdata) w += as_wide(c.value());
      std::cout << v.attribute("xml:lang").as_string(nullptr) << " " << as_utf8(w) << " missing=" << (v.attribute("lang").as_string(nullptr)==nullptr) << "\n"; }
   out("tmx reload saved", save(e, "\t", format_default));
 }
 { const char* bad = "<a><b></a>"; xml_document d; auto r = d.load_buffer(bad, strlen(bad), F); out("bad", std::to_string(r.status) + " " + r.description());
   const char* empty = "   "; auto r2 = d.load_buffer(empty, 3, parse_default); out("empty", std::to_string(r2.status));
   auto r3 = d.load_file("/nonexistent.xml"); out("nofile", std::to_string(r3.status));
   const char* resx = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<!DOCTYPE r [\n <!ENTITY e \"ent\">\n]>\n<root>\n  <data name=\"A\" xml:space=\"preserve\"><value>&e; &#x41; &#169;</value></data>\n</root>";
   auto r4 = d.load_buffer(resx, strlen(resx), F); out("resx", r4.description()); out("resx save", save(d, "\t", format_raw));
 }
}
