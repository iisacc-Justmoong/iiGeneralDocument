// Adapted from first-party iiHtmlBlock/Modifier/DivideBlock; standard C++
// parser, UI bindings removed.
#pragma once

#include "Codec/Private/Markup/GetTagInfo.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace ii::document::detail::blocks {

class DivideBlock {

  public:
    struct ElementInfo {
        std::string tag_name;
        std::string value;
        std::string raw;
        std::size_t raw_begin;
        std::size_t value_begin;
        std::size_t value_end;
        std::size_t raw_end;
        bool is_block;
        bool has_display_override;
        std::string display_value;
    };

    explicit DivideBlock();

    [[nodiscard]] bool Parse(const char *html);
    [[nodiscard]] bool Parse(std::string_view html);

    [[nodiscard]] const std::vector<ElementInfo> &GetElements() const;
    [[nodiscard]] const std::vector<ElementInfo> &GetBlockElements() const;
    [[nodiscard]] const std::string &GetError() const;

    [[nodiscard]] static bool IsBlockTag(std::string_view tag_name);
    [[nodiscard]] static bool IsBlockElement(const GetTagInfo::RangeInfo &element);

  private:
    void Clear();

    std::vector<ElementInfo> elements_;
    std::vector<ElementInfo> block_elements_;
    std::string error_;
};

} // namespace ii::document::detail::blocks
