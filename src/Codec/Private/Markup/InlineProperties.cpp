// Adapted from first-party iiXml/Elements/InlineProperties; standard C++
// parser, UI bindings removed.
#include "InlineProperties.h"

#include "Codec/Private/Markup/Diagnostics.h"

#include <cstddef>
#include <exception>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

bool is_space(char value) {
    return value == ' ' || value == '\n' || value == '\r' || value == '\t';
}

bool is_alpha(char value) {
    return ('a' <= value && value <= 'z') || ('A' <= value && value <= 'Z');
}

bool is_digit(char value) { return '0' <= value && value <= '9'; }

bool is_name_start(char value) { return is_alpha(value) || value == '_' || value == ':'; }

bool is_name_char(char value) {
    return is_name_start(value) || is_digit(value) || value == '-' || value == '.';
}

void skip_spaces(std::string_view input, std::size_t &index, std::size_t end) {
    while (index < end && is_space(input[index])) {
        ++index;
    }
}

bool looks_like_int(std::string_view value) {
    if (value.empty()) {
        return false;
    }

    std::size_t index = 0;
    if (value[index] == '+' || value[index] == '-') {
        ++index;
    }

    if (index == value.size()) {
        return false;
    }

    for (; index < value.size(); ++index) {
        if (!is_digit(value[index])) {
            return false;
        }
    }

    return true;
}

bool looks_like_float(std::string_view value) {
    if (value.empty()) {
        return false;
    }

    std::size_t index = 0;
    if (value[index] == '+' || value[index] == '-') {
        ++index;
    }

    bool saw_digit = false;
    bool saw_dot = false;
    while (index < value.size()) {
        if (is_digit(value[index])) {
            saw_digit = true;
            ++index;
            continue;
        }

        if (value[index] == '.' && !saw_dot) {
            saw_dot = true;
            ++index;
            continue;
        }

        break;
    }

    bool saw_exponent = false;
    if (index < value.size() && (value[index] == 'e' || value[index] == 'E')) {
        saw_exponent = true;
        ++index;
        if (index < value.size() && (value[index] == '+' || value[index] == '-')) {
            ++index;
        }

        bool exponent_digit = false;
        while (index < value.size() && is_digit(value[index])) {
            exponent_digit = true;
            ++index;
        }

        if (!exponent_digit) {
            return false;
        }
    }

    return index == value.size() && saw_digit && (saw_dot || saw_exponent);
}

bool looks_like_bool(std::string_view value) { return value == "true" || value == "false"; }

ii::document::detail::markup::Elements::InlinePropertyType infer_type(std::string_view value,
                                                                      bool was_quoted) {
    if (was_quoted) {
        return ii::document::detail::markup::Elements::InlinePropertyType::StringType;
    }

    if (looks_like_bool(value)) {
        return ii::document::detail::markup::Elements::InlinePropertyType::BoolType;
    }

    if (looks_like_int(value)) {
        return ii::document::detail::markup::Elements::InlinePropertyType::IntType;
    }

    if (looks_like_float(value)) {
        return ii::document::detail::markup::Elements::InlinePropertyType::FloatType;
    }

    return ii::document::detail::markup::Elements::InlinePropertyType::StringType;
}

} // namespace

namespace ii::document::detail::markup::Elements {

InlineProperties::InlineProperties() {}

std::optional<std::vector<InlineProperty>>
InlineProperties::Parse(std::string_view opening_tag, std::size_t source_offset) const {

    try {
        if (opening_tag.size() < 2 || opening_tag.front() != '<' || opening_tag.back() != '>') {

            return std::nullopt;
        }

        const std::size_t close_bracket = opening_tag.size() - 1;
        std::size_t index = 1;
        if (index >= close_bracket || !is_name_start(opening_tag[index])) {

            return std::nullopt;
        }

        ++index;
        while (index < close_bracket && is_name_char(opening_tag[index])) {
            ++index;
        }

        std::vector<InlineProperty> properties;
        while (index < close_bracket) {
            skip_spaces(opening_tag, index, close_bracket);
            if (index >= close_bracket) {
                break;
            }

            if (opening_tag[index] == '/') {
                ++index;
                skip_spaces(opening_tag, index, close_bracket);
                if (index == close_bracket) {
                    break;
                }

                return std::nullopt;
            }

            if (!is_name_start(opening_tag[index])) {

                return std::nullopt;
            }

            const std::size_t NameBegin = index;
            ++index;
            while (index < close_bracket && is_name_char(opening_tag[index])) {
                ++index;
            }
            const std::size_t NameEnd = index;

            skip_spaces(opening_tag, index, close_bracket);

            bool HasValue = false;
            bool TypeDeclared = false;
            bool quoted_value = false;
            std::size_t ValueBegin = NameEnd;
            std::size_t ValueEnd = NameEnd;
            InlinePropertyType ValueType = InlinePropertyType::StringType;

            if (index < close_bracket && opening_tag[index] == '=') {
                HasValue = true;
                ++index;
                skip_spaces(opening_tag, index, close_bracket);
                if (index >= close_bracket) {

                    return std::nullopt;
                }

                if (opening_tag[index] == '"' || opening_tag[index] == '\'') {
                    quoted_value = true;
                    const char quote = opening_tag[index];
                    ++index;
                    ValueBegin = index;
                    while (index < close_bracket && opening_tag[index] != quote) {
                        ++index;
                    }
                    if (index >= close_bracket) {

                        return std::nullopt;
                    }
                    ValueEnd = index;
                    ++index;
                } else {
                    ValueBegin = index;
                    while (index < close_bracket && !is_space(opening_tag[index]) &&
                           opening_tag[index] != '/') {
                        ++index;
                    }
                    ValueEnd = index;
                }

                ValueType =
                    infer_type(opening_tag.substr(ValueBegin, ValueEnd - ValueBegin), quoted_value);
            }

            properties.push_back(InlineProperty{
                std::string(opening_tag.substr(NameBegin, NameEnd - NameBegin)),
                source_offset + NameBegin, source_offset + NameEnd, HasValue,
                source_offset + ValueBegin, source_offset + ValueEnd, ValueType, TypeDeclared});
        }

        return properties;
    } catch (const std::exception &exception) {

        return std::nullopt;
    } catch (...) {

        return std::nullopt;
    }
}

} // namespace ii::document::detail::markup::Elements
