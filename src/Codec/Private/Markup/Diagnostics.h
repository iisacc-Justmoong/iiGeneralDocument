#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace ii::document::detail::markup::Diagnostics {

struct SourcePosition {
    std::size_t Offset;
    std::size_t Line;
    std::size_t Column;
};

[[nodiscard]] SourcePosition LocateSourcePosition(std::string_view Input, std::size_t Offset);

[[nodiscard]] std::string SourceContext(std::string_view Input, std::size_t Offset,
                                        std::size_t Radius = 32);

} // namespace ii::document::detail::markup::Diagnostics
