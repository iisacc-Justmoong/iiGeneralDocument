#include "Diagnostics.h"

#include <algorithm>

namespace ii::document::detail::markup::Diagnostics {

SourcePosition LocateSourcePosition(std::string_view input, std::size_t offset) {
    offset = std::min(offset, input.size());

    SourcePosition position{offset, 1, 1};
    for (std::size_t index = 0; index < offset; ++index) {
        if (input[index] == '\n') {
            ++position.Line;
            position.Column = 1;
        } else {
            ++position.Column;
        }
    }

    return position;
}

std::string SourceContext(std::string_view input, std::size_t offset, std::size_t radius) {
    offset = std::min(offset, input.size());
    const std::size_t begin = offset > radius ? offset - radius : 0;
    const std::size_t end = std::min(input.size(), offset + radius);

    std::string context;
    if (begin > 0) {
        context += "...";
    }

    for (char value : input.substr(begin, end - begin)) {
        switch (value) {
        case '\n':
            context += "\\n";
            break;
        case '\r':
            context += "\\r";
            break;
        case '\t':
            context += "\\t";
            break;
        default:
            context += value;
            break;
        }
    }

    if (end < input.size()) {
        context += "...";
    }

    return context;
}

} // namespace ii::document::detail::markup::Diagnostics
