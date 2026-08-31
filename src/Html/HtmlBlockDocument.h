#pragma once

#include "iiGeneralDocument/Export.h"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ii::document {

class HtmlBlockEditor;

struct IIGENERALDOCUMENT_EXPORT HtmlBlockId {
    std::uint64_t value{0};
    auto operator<=>(const HtmlBlockId&) const = default;
};

class IIGENERALDOCUMENT_EXPORT HtmlBlock {
public:
    [[nodiscard]] HtmlBlockId id() const noexcept;
    [[nodiscard]] const std::string& tagName() const noexcept;
    [[nodiscard]] const std::string& value() const noexcept;
    [[nodiscard]] const std::string& html() const noexcept;
    [[nodiscard]] std::size_t rawBegin() const noexcept;
    [[nodiscard]] std::size_t valueBegin() const noexcept;
    [[nodiscard]] std::size_t valueEnd() const noexcept;
    [[nodiscard]] std::size_t rawEnd() const noexcept;
    [[nodiscard]] bool hasDisplayOverride() const noexcept;
    [[nodiscard]] const std::string& displayValue() const noexcept;

private:
    friend class HtmlBlockDocument;
    friend class HtmlBlockEditor;

    HtmlBlock(
        HtmlBlockId id,
        std::size_t sourceIndex,
        std::string tagName,
        std::string value,
        std::string html,
        std::size_t rawBegin,
        std::size_t valueBegin,
        std::size_t valueEnd,
        std::size_t rawEnd,
        bool hasDisplayOverride,
        std::string displayValue);

    HtmlBlockId id_;
    std::size_t sourceIndex_{0};
    std::string tagName_;
    std::string value_;
    std::string html_;
    std::size_t rawBegin_{0};
    std::size_t valueBegin_{0};
    std::size_t valueEnd_{0};
    std::size_t rawEnd_{0};
    bool hasDisplayOverride_{false};
    std::string displayValue_;
};

class IIGENERALDOCUMENT_EXPORT HtmlBlockDocument {
public:
    HtmlBlockDocument() = default;

    [[nodiscard]] static HtmlBlockDocument fromHtml(std::string html);

    [[nodiscard]] const std::string& html() const noexcept;
    [[nodiscard]] const std::vector<HtmlBlock>& blocks() const noexcept;
    [[nodiscard]] const HtmlBlock* find(HtmlBlockId id) const noexcept;
    [[nodiscard]] std::uint64_t revision() const noexcept;

private:
    friend class HtmlBlockEditor;

    [[nodiscard]] static std::vector<HtmlBlock> parseBlocks(std::string_view html);

    std::string html_;
    std::vector<HtmlBlock> blocks_;
    std::uint64_t nextId_{1};
    std::uint64_t revision_{0};
};

} // namespace ii::document
