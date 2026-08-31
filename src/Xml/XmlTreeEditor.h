#pragma once

#include "Xml/XmlTreeDocument.h"
#include "iiGeneralDocument/Export.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ii::document {

class IIGENERALDOCUMENT_EXPORT XmlTreeEditor {
public:
    explicit XmlTreeEditor(XmlTreeDocument& document) noexcept;

    [[nodiscard]] XmlNodeId create(
        std::string nodeXml,
        std::optional<XmlNodeId> parent = std::nullopt);
    [[nodiscard]] const XmlNode* read(XmlNodeId id) const noexcept;
    void update(XmlNodeId id, std::string nodeXml);
    bool remove(XmlNodeId id);

private:
    struct IdentityBinding {
        XmlNodeId id;
        std::size_t begin{0};
        std::size_t end{0};
        bool allocate{false};
    };

    [[nodiscard]] static std::string serializeSingleNode(std::string_view nodeXml);
    void replaceRange(
        std::size_t begin,
        std::size_t end,
        std::string replacement,
        std::vector<IdentityBinding> bindings);

    XmlTreeDocument& document_;
};

} // namespace ii::document
