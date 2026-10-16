// Adapted from first-party iiXml/Elements/OpenTag; standard C++ parser, UI
// bindings removed.
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ii::document::detail::markup::Elements {

struct OpenTagRange {
    std::string TagName;
    std::size_t RawBegin;
    std::size_t ValueBegin;
    std::size_t ValueEnd;
    std::size_t RawEnd;
};

class OpenTag {

  public:
    explicit OpenTag();

    [[nodiscard]] bool CloseOpenTag(std::vector<std::string> &OpenTags,
                                    std::string_view ClosingTagName) const;

    [[nodiscard]] std::optional<std::vector<OpenTagRange>>
    ParseOpenTags(std::string_view Input) const;
};

} // namespace ii::document::detail::markup::Elements

using OpenTag = ii::document::detail::markup::Elements::OpenTag;
