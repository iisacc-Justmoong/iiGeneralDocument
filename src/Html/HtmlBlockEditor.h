#pragma once

#include "Html/HtmlBlockDocument.h"
#include "iiGeneralDocument/Export.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace ii::document {

class IIGENERALDOCUMENT_EXPORT HtmlBlockEditor {
public:
    explicit HtmlBlockEditor(HtmlBlockDocument& document) noexcept;

    [[nodiscard]] HtmlBlockId create(
        std::string blockHtml,
        std::optional<HtmlBlockId> parent = std::nullopt);
    [[nodiscard]] const HtmlBlock* read(HtmlBlockId id) const noexcept;
    void update(HtmlBlockId id, std::string blockHtml);
    bool remove(HtmlBlockId id);

private:
    [[nodiscard]] static std::string serializeSingleBlock(std::string_view blockHtml);
    void replaceRange(
        std::size_t begin,
        std::size_t end,
        std::string replacement,
        std::optional<HtmlBlockId> replacementRootId,
        bool replacementRootIsNew);

    HtmlBlockDocument& document_;
};

} // namespace ii::document
