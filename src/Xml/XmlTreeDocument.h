#pragma once

#include "iiGeneralDocument/Export.h"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ii::document {

class XmlTreeEditor;

struct IIGENERALDOCUMENT_EXPORT XmlNodeId {
    std::uint64_t value{0};
    auto operator<=>(const XmlNodeId&) const = default;
};

enum class XmlAttributeValueType {
    string,
    integer,
    real,
    boolean,
};

class IIGENERALDOCUMENT_EXPORT XmlAttribute {
public:
    [[nodiscard]] const std::string& name() const noexcept;
    [[nodiscard]] bool hasValue() const noexcept;
    [[nodiscard]] const std::string& value() const noexcept;
    [[nodiscard]] XmlAttributeValueType valueType() const noexcept;
    [[nodiscard]] bool typeDeclared() const noexcept;

private:
    friend class XmlTreeDocument;

    XmlAttribute(
        std::string name,
        bool hasValue,
        std::string value,
        XmlAttributeValueType valueType,
        bool typeDeclared);

    std::string name_;
    bool hasValue_{false};
    std::string value_;
    XmlAttributeValueType valueType_{XmlAttributeValueType::string};
    bool typeDeclared_{false};
};

class IIGENERALDOCUMENT_EXPORT XmlNode {
public:
    [[nodiscard]] XmlNodeId id() const noexcept;
    [[nodiscard]] const std::optional<XmlNodeId>& parentId() const noexcept;
    [[nodiscard]] const std::string& name() const noexcept;
    [[nodiscard]] const std::string& innerXml() const noexcept;
    [[nodiscard]] const std::string& rawXml() const noexcept;
    [[nodiscard]] const std::vector<XmlAttribute>& attributes() const noexcept;
    [[nodiscard]] const std::vector<XmlNodeId>& childIds() const noexcept;
    [[nodiscard]] bool isSelfClosing() const noexcept;
    [[nodiscard]] std::size_t rawBegin() const noexcept;
    [[nodiscard]] std::size_t valueBegin() const noexcept;
    [[nodiscard]] std::size_t valueEnd() const noexcept;
    [[nodiscard]] std::size_t rawEnd() const noexcept;

private:
    friend class XmlTreeDocument;
    friend class XmlTreeEditor;

    XmlNode(
        XmlNodeId id,
        std::optional<XmlNodeId> parentId,
        std::string name,
        std::string innerXml,
        std::string rawXml,
        std::vector<XmlAttribute> attributes,
        std::vector<XmlNodeId> childIds,
        bool selfClosing,
        std::size_t rawBegin,
        std::size_t valueBegin,
        std::size_t valueEnd,
        std::size_t rawEnd);

    XmlNodeId id_;
    std::optional<XmlNodeId> parentId_;
    std::string name_;
    std::string innerXml_;
    std::string rawXml_;
    std::vector<XmlAttribute> attributes_;
    std::vector<XmlNodeId> childIds_;
    bool selfClosing_{false};
    std::size_t rawBegin_{0};
    std::size_t valueBegin_{0};
    std::size_t valueEnd_{0};
    std::size_t rawEnd_{0};
};

class IIGENERALDOCUMENT_EXPORT XmlTreeDocument {
public:
    XmlTreeDocument() = default;

    [[nodiscard]] static XmlTreeDocument fromXml(std::string xml);

    [[nodiscard]] const std::string& xml() const noexcept;
    [[nodiscard]] const std::vector<XmlNode>& nodes() const noexcept;
    [[nodiscard]] const std::optional<XmlNodeId>& rootId() const noexcept;
    [[nodiscard]] const XmlNode* find(XmlNodeId id) const noexcept;
    [[nodiscard]] std::uint64_t revision() const noexcept;

private:
    friend class XmlTreeEditor;

    [[nodiscard]] static std::vector<XmlNode> parseNodes(
        std::string_view xml,
        bool allowEmpty);

    std::string xml_;
    std::vector<XmlNode> nodes_;
    std::optional<XmlNodeId> rootId_;
    std::uint64_t nextId_{1};
    std::uint64_t revision_{0};
};

} // namespace ii::document
