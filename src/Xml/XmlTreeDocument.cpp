#include "Xml/XmlTreeDocument.h"

#include "Core/Diagnostic.h"

#include <iiXml>

#include <algorithm>
#include <limits>
#include <string_view>
#include <utility>

namespace ii::document {
namespace {

bool isAsciiSpace(char value) noexcept
{
    return value == ' ' || value == '\n' || value == '\r' || value == '\t';
}

std::size_t skipAsciiSpace(std::string_view source, std::size_t offset) noexcept
{
    while (offset < source.size() && isAsciiSpace(source[offset])) {
        ++offset;
    }
    return offset;
}

bool startsWithAt(std::string_view source, std::size_t offset, std::string_view token) noexcept
{
    return offset <= source.size() && source.substr(offset).starts_with(token);
}

bool startsWithXmlDeclaration(std::string_view source, std::size_t offset) noexcept
{
    constexpr std::string_view prefix = "<?xml";
    if (!startsWithAt(source, offset, prefix)) {
        return false;
    }
    const std::size_t next = offset + prefix.size();
    return next == source.size()
        || isAsciiSpace(source[next])
        || source[next] == '?';
}

std::size_t requireTokenEnd(
    std::string_view source,
    std::size_t offset,
    std::string_view token,
    const char* description)
{
    const std::size_t end = source.find(token, offset);
    if (end == std::string_view::npos) {
        throw DocumentError(std::string("XML ") + description + " is not closed");
    }
    return end + token.size();
}

std::size_t declarationLength(
    std::string_view declaration,
    iiXml::Elements::DoctypeKind expectedKind,
    bool standardDoctype)
{
    std::string normalized;
    std::string_view parsedDeclaration = declaration;
    if (standardDoctype) {
        normalized.assign(declaration);
        normalized.replace(2, 7, "Doctype");
        parsedDeclaration = normalized;
    }

    const iiXml::Elements::Doctype matcher;
    const iiXml::Elements::DoctypeResult result = matcher.MatchTopResult(parsedDeclaration);
    if (result.Status != iiXml::Elements::DoctypeStatus::Matched
        || !result.Match.has_value()
        || result.Match->Kind != expectedKind) {
        std::string reason = result.Reason;
        if (reason.empty()) {
            reason = "iiXml rejected the declaration";
        }
        throw DocumentError("XML prolog parse failed: " + reason);
    }
    return result.Match->Raw.size();
}

std::size_t documentBodyBegin(std::string_view source)
{
    std::size_t offset = 0;
    if (source.size() >= 3
        && static_cast<unsigned char>(source[0]) == 0xef
        && static_cast<unsigned char>(source[1]) == 0xbb
        && static_cast<unsigned char>(source[2]) == 0xbf) {
        offset = 3;
    }
    offset = skipAsciiSpace(source, offset);

    bool hasXmlDeclaration = false;
    bool hasDoctype = false;
    while (offset < source.size()) {
        if (startsWithXmlDeclaration(source, offset)) {
            if (hasXmlDeclaration || hasDoctype) {
                throw DocumentError("XML declaration must occur once before the doctype");
            }
            offset += declarationLength(
                source.substr(offset), iiXml::Elements::DoctypeKind::XmlDeclaration, false);
            hasXmlDeclaration = true;
        } else if (startsWithAt(source, offset, "<!Doctype")) {
            if (hasDoctype) {
                throw DocumentError("XML document contains more than one doctype");
            }
            offset += declarationLength(
                source.substr(offset), iiXml::Elements::DoctypeKind::DoctypeDeclaration, false);
            hasDoctype = true;
        } else if (startsWithAt(source, offset, "<!DOCTYPE")) {
            if (hasDoctype) {
                throw DocumentError("XML document contains more than one doctype");
            }
            offset += declarationLength(
                source.substr(offset), iiXml::Elements::DoctypeKind::DoctypeDeclaration, true);
            hasDoctype = true;
        } else if (startsWithAt(source, offset, "<!--")) {
            offset = requireTokenEnd(source, offset + 4, "-->", "comment");
        } else if (startsWithAt(source, offset, "<?")) {
            offset = requireTokenEnd(
                source, offset + 2, "?>", "processing instruction");
        } else {
            break;
        }
        offset = skipAsciiSpace(source, offset);
    }
    return offset;
}

bool containsOnlyXmlMisc(std::string_view source) noexcept
{
    std::size_t offset = 0;
    while (offset < source.size()) {
        offset = skipAsciiSpace(source, offset);
        if (offset == source.size()) {
            return true;
        }
        if (startsWithAt(source, offset, "<!--")) {
            const std::size_t end = source.find("-->", offset + 4);
            if (end == std::string_view::npos) {
                return false;
            }
            offset = end + 3;
            continue;
        }
        if (startsWithXmlDeclaration(source, offset)) {
            return false;
        }
        if (startsWithAt(source, offset, "<?")) {
            const std::size_t end = source.find("?>", offset + 2);
            if (end == std::string_view::npos) {
                return false;
            }
            offset = end + 2;
            continue;
        }
        return false;
    }
    return true;
}

void validateNodeHierarchy(
    const iiXml::Parser::TagNode& node,
    std::size_t sourceSize)
{
    const auto& range = node.Range;
    if (range.RawBegin >= range.RawEnd
        || range.RawEnd > sourceSize
        || range.ValueBegin < range.RawBegin
        || range.ValueBegin > range.ValueEnd
        || range.ValueEnd > range.RawEnd) {
        throw DocumentError("iiXml produced an invalid XML node range");
    }

    std::size_t previousEnd = range.ValueBegin;
    for (const auto& child : node.Children) {
        if (child.Range.RawBegin < range.ValueBegin
            || child.Range.RawEnd > range.ValueEnd) {
            throw DocumentError("XML child range escapes its parent value range");
        }
        if (child.Range.RawBegin < previousEnd) {
            throw DocumentError("XML hierarchy contains crossing or overlapping sibling ranges");
        }
        validateNodeHierarchy(child, sourceSize);
        previousEnd = child.Range.RawEnd;
    }
}

XmlAttributeValueType publicValueType(iiXml::Elements::InlinePropertyType valueType) noexcept
{
    switch (valueType) {
    case iiXml::Elements::InlinePropertyType::IntType:
        return XmlAttributeValueType::integer;
    case iiXml::Elements::InlinePropertyType::FloatType:
        return XmlAttributeValueType::real;
    case iiXml::Elements::InlinePropertyType::BoolType:
        return XmlAttributeValueType::boolean;
    case iiXml::Elements::InlinePropertyType::StringType:
        return XmlAttributeValueType::string;
    }
    return XmlAttributeValueType::string;
}

} // namespace

XmlAttribute::XmlAttribute(
    std::string name,
    bool hasValue,
    std::string value,
    XmlAttributeValueType valueType,
    bool typeDeclared)
    : name_(std::move(name)),
      hasValue_(hasValue),
      value_(std::move(value)),
      valueType_(valueType),
      typeDeclared_(typeDeclared)
{
}

const std::string& XmlAttribute::name() const noexcept
{
    return name_;
}

bool XmlAttribute::hasValue() const noexcept
{
    return hasValue_;
}

const std::string& XmlAttribute::value() const noexcept
{
    return value_;
}

XmlAttributeValueType XmlAttribute::valueType() const noexcept
{
    return valueType_;
}

bool XmlAttribute::typeDeclared() const noexcept
{
    return typeDeclared_;
}

XmlNode::XmlNode(
    XmlNodeId id,
    std::optional<XmlNodeId> parentId,
    std::size_t depth,
    std::string name,
    std::string innerXml,
    std::string rawXml,
    std::vector<XmlAttribute> attributes,
    std::vector<XmlNodeId> childIds,
    bool selfClosing,
    std::size_t rawBegin,
    std::size_t valueBegin,
    std::size_t valueEnd,
    std::size_t rawEnd)
    : id_(id),
      parentId_(parentId),
      depth_(depth),
      name_(std::move(name)),
      innerXml_(std::move(innerXml)),
      rawXml_(std::move(rawXml)),
      attributes_(std::move(attributes)),
      childIds_(std::move(childIds)),
      selfClosing_(selfClosing),
      rawBegin_(rawBegin),
      valueBegin_(valueBegin),
      valueEnd_(valueEnd),
      rawEnd_(rawEnd)
{
}

XmlNodeId XmlNode::id() const noexcept
{
    return id_;
}

const std::optional<XmlNodeId>& XmlNode::parentId() const noexcept
{
    return parentId_;
}

std::size_t XmlNode::depth() const noexcept
{
    return depth_;
}

const std::string& XmlNode::name() const noexcept
{
    return name_;
}

const std::string& XmlNode::innerXml() const noexcept
{
    return innerXml_;
}

const std::string& XmlNode::rawXml() const noexcept
{
    return rawXml_;
}

std::string_view XmlNode::openingTag() const noexcept
{
    if (valueBegin_ < rawBegin_ || valueBegin_ - rawBegin_ > rawXml_.size()) {
        return {};
    }
    return std::string_view(rawXml_).substr(0, valueBegin_ - rawBegin_);
}

std::string_view XmlNode::closingTag() const noexcept
{
    if (valueEnd_ < rawBegin_ || valueEnd_ - rawBegin_ > rawXml_.size()) {
        return {};
    }
    return std::string_view(rawXml_).substr(valueEnd_ - rawBegin_);
}

const std::vector<XmlAttribute>& XmlNode::attributes() const noexcept
{
    return attributes_;
}

const std::vector<XmlNodeId>& XmlNode::childIds() const noexcept
{
    return childIds_;
}

bool XmlNode::isSelfClosing() const noexcept
{
    return selfClosing_;
}

std::size_t XmlNode::rawBegin() const noexcept
{
    return rawBegin_;
}

std::size_t XmlNode::valueBegin() const noexcept
{
    return valueBegin_;
}

std::size_t XmlNode::valueEnd() const noexcept
{
    return valueEnd_;
}

std::size_t XmlNode::rawEnd() const noexcept
{
    return rawEnd_;
}

XmlTreeDocument XmlTreeDocument::fromXml(std::string xml)
{
    XmlTreeDocument document;
    document.nodes_ = parseNodes(xml, false);
    document.xml_ = std::move(xml);
    document.rootId_ = document.nodes_.front().id();

    if (document.nodes_.size() >= std::numeric_limits<std::uint64_t>::max()) {
        throw DocumentError("XML node id space is exhausted");
    }
    document.nextId_ = static_cast<std::uint64_t>(document.nodes_.size()) + 1;
    return document;
}

const std::string& XmlTreeDocument::xml() const noexcept
{
    return xml_;
}

const std::vector<XmlNode>& XmlTreeDocument::nodes() const noexcept
{
    return nodes_;
}

const std::optional<XmlNodeId>& XmlTreeDocument::rootId() const noexcept
{
    return rootId_;
}

const XmlNode* XmlTreeDocument::find(XmlNodeId id) const noexcept
{
    const auto found = std::ranges::find_if(nodes_, [id](const XmlNode& node) {
        return node.id() == id;
    });
    return found == nodes_.end() ? nullptr : &*found;
}

std::uint64_t XmlTreeDocument::revision() const noexcept
{
    return revision_;
}

std::vector<XmlNode> XmlTreeDocument::parseNodes(
    std::string_view xml,
    bool allowEmpty)
{
    const std::size_t bodyBegin = documentBodyBegin(xml);
    const std::string_view body = xml.substr(bodyBegin);
    if (containsOnlyXmlMisc(body)) {
        if (allowEmpty) {
            return {};
        }
        throw DocumentError("XML document must contain exactly one root element");
    }

    const iiXml::Parser::TagParser parser;
    const iiXml::Parser::TagDocumentResult result = parser.ParseAllDocumentResult(body);
    if (result.Status != iiXml::Parser::TagTreeParseStatus::Parsed
        || !result.Document.has_value()) {
        std::string reason = result.Diagnostic.Reason;
        if (reason.empty()) {
            reason = "iiXml rejected the source";
        }
        throw DocumentError("XML tree parse failed: " + reason);
    }

    const iiXml::Parser::TagDocument& parsed = *result.Document;
    if (parsed.Nodes.size() != 1) {
        throw DocumentError("XML document must contain exactly one root element");
    }

    const iiXml::Parser::TagNode& root = parsed.Nodes.front();
    if (root.Range.RawBegin != 0
        || !containsOnlyXmlMisc(body.substr(root.Range.RawEnd))) {
        throw DocumentError("XML document contains content outside its root element");
    }
    validateNodeHierarchy(root, body.size());

    std::vector<XmlNode> nodes;
    const auto appendNode = [&](const auto& self,
                                const iiXml::Parser::TagNode& parsedNode,
                                std::optional<XmlNodeId> parentId,
                                std::size_t depth) -> XmlNodeId {
        if (nodes.size() >= std::numeric_limits<std::uint64_t>::max()) {
            throw DocumentError("XML node id space is exhausted");
        }
        const XmlNodeId id{static_cast<std::uint64_t>(nodes.size()) + 1};

        std::vector<XmlAttribute> attributes;
        attributes.reserve(parsedNode.Fields.size());
        for (const auto& field : parsedNode.Fields) {
            attributes.push_back(XmlAttribute(
                field.Name,
                field.HasValue,
                field.HasValue ? std::string(parsed.FieldValueView(field)) : std::string{},
                publicValueType(field.ValueType),
                field.TypeDeclared));
        }

        const std::size_t rawBegin = bodyBegin + parsedNode.Range.RawBegin;
        const std::size_t valueBegin = bodyBegin + parsedNode.Range.ValueBegin;
        const std::size_t valueEnd = bodyBegin + parsedNode.Range.ValueEnd;
        const std::size_t rawEnd = bodyBegin + parsedNode.Range.RawEnd;
        const bool selfClosing = parsedNode.Range.ValueBegin == parsedNode.Range.RawEnd
            && parsedNode.Range.ValueEnd == parsedNode.Range.RawEnd;

        const std::size_t index = nodes.size();
        nodes.push_back(XmlNode(
            id,
            parentId,
            depth,
            parsedNode.Range.TagName,
            std::string(xml.substr(valueBegin, valueEnd - valueBegin)),
            std::string(xml.substr(rawBegin, rawEnd - rawBegin)),
            std::move(attributes),
            {},
            selfClosing,
            rawBegin,
            valueBegin,
            valueEnd,
            rawEnd));

        for (const auto& child : parsedNode.Children) {
            if (depth == std::numeric_limits<std::size_t>::max()) {
                throw DocumentError("XML hierarchy depth is exhausted");
            }
            const XmlNodeId childId = self(self, child, id, depth + 1);
            nodes[index].childIds_.push_back(childId);
        }
        return id;
    };

    static_cast<void>(appendNode(appendNode, root, std::nullopt, 0));
    return nodes;
}

} // namespace ii::document
