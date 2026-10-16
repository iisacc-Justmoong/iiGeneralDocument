#pragma once
#include "Codec/Private/XmlBytes.h"
#include "Core/Diagnostic.h"
#include <map>
#include <pugixml/pugixml.hpp>
#include <set>
#include <sstream>

namespace ii::document::detail {
inline std::string localName(std::string_view name) {
    auto i = name.find(':');
    return std::string(i == name.npos ? name : name.substr(i + 1));
}
inline std::string namespaceOf(pugi::xml_node node, std::string_view name, bool attribute = false) {
    auto i = name.find(':');
    if (i == name.npos && attribute)
        return {};
    auto prefix = i == name.npos ? std::string{} : std::string(name.substr(0, i));
    if (prefix == "xml")
        return "http://www.w3.org/XML/1998/namespace";
    if (prefix == "xmlns" || name == "xmlns")
        return "http://www.w3.org/2000/xmlns/";
    auto key = prefix.empty() ? "xmlns" : "xmlns:" + prefix;
    for (auto n = node; n; n = n.parent())
        if (auto a = n.attribute(key.c_str())) {
            if (!prefix.empty() && std::string_view(a.value()).empty())
                throw DocumentError("Empty XML namespace binding");
            return a.value();
        }
    if (!prefix.empty())
        throw DocumentError("Unbound XML namespace prefix: " + prefix);
    return {};
}
struct XmlAttribute {
    Text local, uri, content;
    Text name() const { return local; }
    Text value() const { return content; }
};
class XmlAttributes : public std::vector<XmlAttribute> {
  public:
    Text value(const Text &uri, const Text &name) const {
        for (const auto &a : *this)
            if (a.uri == uri && a.local == name)
                return a.content;
        return {};
    }
    Text value(const Text &name) const {
        for (const auto &a : *this)
            if (a.local == name)
                return a.content;
        return {};
    }
};
class XmlReader {
  public:
    enum TokenType { StartDocument, StartElement, Characters, EndElement, DTD, EndDocument };

  private:
    pugi::xml_document document_;
    struct Event {
        TokenType token;
        pugi::xml_node node;
    };
    std::vector<Event> events_;
    std::size_t index_{};
    Text error_;
    void validate(pugi::xml_node node) {
        (void)namespaceOf(node, node.name());
        std::set<std::pair<std::string, std::string>> names;
        for (auto a : node.attributes()) {
            std::string_view name = a.name();
            if (name == "xmlns" || name.starts_with("xmlns:"))
                continue;
            if (!names.emplace(namespaceOf(node, name, true), localName(name)).second)
                throw DocumentError("Duplicate expanded XML attribute");
        }
    }

  public:
    explicit XmlReader(const ByteBuffer &bytes) {
        events_.push_back({StartDocument, {}});
        try {
            auto source = xmlUtf8(bytes);
            validateXmlEntities(source);
            auto parsed = document_.load_buffer(source.data(), source.size(),
                                                pugi::parse_full | pugi::parse_ws_pcdata,
                                                pugi::encoding_utf8);
            if (!parsed)
                throw DocumentError(std::string(parsed.description()) + " at byte " +
                                    std::to_string(parsed.offset));
            // Walk without recursion. Format-level byte limits apply before this
            // reader.
            struct Frame {
                pugi::xml_node node;
                bool end;
                std::size_t depth;
            };
            std::vector<Frame> pending;
            for (auto n = document_.last_child(); n; n = n.previous_sibling())
                pending.push_back({n, false, 0});
            std::size_t roots = 0;
            while (!pending.empty()) {
                auto [n, end, depth] = pending.back();
                pending.pop_back();
                if (end) {
                    events_.push_back({EndElement, n});
                    continue;
                }
                if (depth > 1024)
                    throw DocumentError("XML nesting exceeds 1024 elements");
                switch (n.type()) {
                case pugi::node_element:
                    if (depth == 0)
                        ++roots;
                    validate(n);
                    events_.push_back({StartElement, n});
                    pending.push_back({n, true, depth});
                    for (auto child = n.last_child(); child; child = child.previous_sibling())
                        pending.push_back({child, false, depth + 1});
                    break;
                case pugi::node_pcdata:
                case pugi::node_cdata:
                    if (depth == 0 && !Text(n.value()).trimmed().isEmpty())
                        throw DocumentError("Text outside XML root");
                    events_.push_back({Characters, n});
                    break;
                case pugi::node_doctype:
                    events_.push_back({DTD, n});
                    break;
                default:
                    break;
                }
            }
            if (roots != 1)
                throw DocumentError("XML must contain one root element");
        } catch (const std::exception &exception) {
            error_ = Text(exception.what());
        }
        events_.push_back({EndDocument, {}});
    }
    bool atEnd() const { return hasError() || index_ + 1 >= events_.size(); }
    bool hasError() const { return !error_.isEmpty(); }
    Text errorString() const { return error_; }
    void raiseError(const Text &value) { error_ = value; }
    TokenType tokenType() const { return events_[index_].token; }
    bool isStartElement() const { return tokenType() == StartElement; }
    bool isEndElement() const { return tokenType() == EndElement; }
    bool isCharacters() const { return tokenType() == Characters; }
    TokenType readNext() {
        if (!atEnd())
            ++index_;
        return tokenType();
    }
    bool readNextStartElement() {
        while (!atEnd()) {
            readNext();
            if (isStartElement())
                return true;
            if (isEndElement())
                return false;
        }
        return false;
    }
    Text name() const { return localName(events_[index_].node.name()); }
    Text namespaceUri() const {
        auto n = events_[index_].node;
        return n ? Text(namespaceOf(n, n.name())) : Text{};
    }
    Text text() const { return Text(events_[index_].node.value()); }
    XmlAttributes attributes() const {
        XmlAttributes values;
        auto n = events_[index_].node;
        for (auto a : n.attributes()) {
            std::string_view name = a.name();
            if (name == "xmlns" || name.starts_with("xmlns:"))
                continue;
            values.push_back(
                {Text(localName(name)), Text(namespaceOf(n, name, true)), Text(a.value())});
        }
        return values;
    }
    void skipCurrentElement() {
        if (!isStartElement())
            return;
        std::size_t depth = 1;
        while (depth && !atEnd()) {
            readNext();
            if (isStartElement())
                ++depth;
            else if (isEndElement())
                --depth;
        }
    }
    Text readElementText() {
        Text value;
        if (!isStartElement())
            return value;
        while (!atEnd()) {
            readNext();
            if (isEndElement())
                return value;
            if (isStartElement()) {
                raiseError(Text("Unexpected child in XML text element"));
                return value;
            }
            if (isCharacters())
                value += text();
        }
        return value;
    }
};
class XmlWriter {
    pugi::xml_document document_;
    std::vector<pugi::xml_node> stack_;
    ByteBuffer &output_;
    bool empty_{};
    std::map<std::string, std::string> prefixes_{{"http://www.w3.org/XML/1998/namespace", "xml"}};
    void finishEmpty() {
        if (empty_) {
            stack_.pop_back();
            empty_ = false;
        }
    }
    std::string qualified(const Text &uri, const Text &name) const {
        auto ns = uri.toStdString(), local = name.toStdString();
        auto found = prefixes_.find(ns);
        if (found == prefixes_.end())
            throw DocumentError("XML writer namespace was not declared: " + ns);
        return found->second.empty() ? local : found->second + ":" + local;
    }
    pugi::xml_node current() const {
        if (stack_.empty())
            throw DocumentError("XML writer has no open element");
        return stack_.back();
    }

  public:
    explicit XmlWriter(ByteBuffer &output) : output_(output) {}
    void setAutoFormatting(bool) {}
    void writeStartDocument(const Text &version) {
        auto n = document_.append_child(pugi::node_declaration);
        n.append_attribute("version") = version.toStdString().c_str();
        n.append_attribute("encoding") = "UTF-8";
    }
    void writeStartElement(const Text &name) {
        finishEmpty();
        auto parent = stack_.empty() ? pugi::xml_node(document_) : stack_.back();
        stack_.push_back(parent.append_child(name.toStdString().c_str()));
    }
    void writeStartElement(const Text &uri, const Text &name) {
        writeStartElement(Text(qualified(uri, name)));
    }
    void writeEndElement() {
        finishEmpty();
        if (stack_.empty())
            throw DocumentError("Unbalanced XML writer close");
        stack_.pop_back();
    }
    void writeEmptyElement(const Text &name) {
        writeStartElement(name);
        empty_ = true;
    }
    void writeEmptyElement(const Text &uri, const Text &name) {
        writeStartElement(uri, name);
        empty_ = true;
    }
    void writeNamespace(const Text &uri, const Text &prefix) {
        auto p = prefix.toStdString();
        prefixes_[uri.toStdString()] = p;
        current().append_attribute((p.empty() ? "xmlns" : "xmlns:" + p).c_str()) =
            uri.toStdString().c_str();
    }
    void writeDefaultNamespace(const Text &uri) { writeNamespace(uri, Text{}); }
    void writeAttribute(const Text &name, const Text &value) {
        validateXmlText(value);
        current().append_attribute(name.toStdString().c_str()) = value.toStdString().c_str();
    }
    void writeAttribute(const Text &uri, const Text &name, const Text &value) {
        writeAttribute(Text(qualified(uri, name)), value);
    }
    void writeCharacters(const Text &value) {
        validateXmlText(value);
        finishEmpty();
        current().append_child(pugi::node_pcdata).set_value(value.toStdString().c_str());
    }
    void writeTextElement(const Text &name, const Text &value) {
        writeStartElement(name);
        writeCharacters(value);
        writeEndElement();
    }
    void writeTextElement(const Text &uri, const Text &name, const Text &value) {
        writeStartElement(uri, name);
        writeCharacters(value);
        writeEndElement();
    }
    void writeEndDocument() {
        finishEmpty();
        stack_.clear();
        std::ostringstream stream;
        document_.save(stream, "", pugi::format_raw | pugi::format_no_declaration,
                       pugi::encoding_utf8);
        output_ = stream.str();
    }
};
} // namespace ii::document::detail
