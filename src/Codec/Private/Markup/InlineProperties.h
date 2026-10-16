// Adapted from first-party iiXml/Elements/InlineProperties; standard C++
// parser, UI bindings removed.
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ii::document::detail::markup::Elements {

enum class InlinePropertyType { StringType, IntType, FloatType, BoolType };

struct InlineProperty {
    std::string Name;
    std::size_t NameBegin;
    std::size_t NameEnd;
    bool HasValue;
    std::size_t ValueBegin;
    std::size_t ValueEnd;
    InlinePropertyType ValueType;
    bool TypeDeclared;
};

class InlineProperties {

  public:
    explicit InlineProperties();

    [[nodiscard]] std::optional<std::vector<InlineProperty>>
    Parse(std::string_view OpeningTag, std::size_t SourceOffset = 0) const;
};

} // namespace ii::document::detail::markup::Elements

using InlineProperties = ii::document::detail::markup::Elements::InlineProperties;
