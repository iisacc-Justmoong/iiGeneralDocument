// Adapted from first-party iiHtmlBlock/Modifier/DivideBlock; standard C++
// parser, UI bindings removed.
#include "DivideBlock.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <exception>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace ii::document::detail::blocks {

namespace {

char ToLowerAsciiChar(char character) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
}

std::string ToLowerAscii(std::string_view value) {
    std::string lowered;
    lowered.reserve(value.size());

    for (char character : value) {
        lowered.push_back(ToLowerAsciiChar(character));
    }

    return lowered;
}

std::string TrimAscii(std::string_view value) {
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) {
        ++begin;
    }

    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }

    return std::string(value.substr(begin, end - begin));
}

std::string CollapseAsciiWhitespace(std::string_view value) {
    std::string collapsed;
    collapsed.reserve(value.size());

    bool previous_space = false;
    for (char character : value) {
        if (std::isspace(static_cast<unsigned char>(character))) {
            if (!previous_space) {
                collapsed.push_back(' ');
                previous_space = true;
            }
            continue;
        }

        collapsed.push_back(character);
        previous_space = false;
    }

    return collapsed;
}

std::string NormalizeDisplayValue(std::string_view value) {
    std::string normalized = ToLowerAscii(TrimAscii(value));
    const std::string important = "!important";
    const std::size_t important_position = normalized.find(important);
    if (important_position != std::string::npos) {
        normalized = TrimAscii(std::string_view(normalized).substr(0, important_position));
    }

    return CollapseAsciiWhitespace(normalized);
}

bool IsBlockDisplayValue(std::string_view value) {
    const std::string display = NormalizeDisplayValue(value);
    if (display.empty()) {
        return false;
    }

    if (display == "block" || display == "flow-root" || display == "list-item" ||
        display == "flex" || display == "grid" || display == "table") {
        return true;
    }

    return display.rfind("block ", 0) == 0;
}

std::optional<std::string> FindStyleDisplayValue(std::string_view style) {
    std::optional<std::string> display;
    std::size_t declaration_begin = 0;
    while (declaration_begin <= style.size()) {
        const std::size_t declaration_end = style.find(';', declaration_begin);
        const std::size_t end =
            declaration_end == std::string_view::npos ? style.size() : declaration_end;
        const std::string declaration =
            TrimAscii(style.substr(declaration_begin, end - declaration_begin));
        const std::size_t separator = declaration.find(':');

        if (separator != std::string::npos) {
            const std::string property =
                ToLowerAscii(TrimAscii(std::string_view(declaration).substr(0, separator)));
            if (property == "display") {
                display = TrimAscii(std::string_view(declaration).substr(separator + 1));
            }
        }

        if (declaration_end == std::string_view::npos) {
            break;
        }
        declaration_begin = declaration_end + 1;
    }

    return display;
}

std::optional<std::string> FindDisplayOverride(const std::vector<GetTagInfo::FieldInfo> &fields) {
    std::optional<std::string> display;

    for (const GetTagInfo::FieldInfo &field : fields) {
        const std::string name = ToLowerAscii(field.name);
        if (name == "style" && field.has_value) {
            const std::optional<std::string> style_display = FindStyleDisplayValue(field.value);
            if (style_display.has_value()) {
                display = *style_display;
            }
            continue;
        }

        if (name == "display" && field.has_value) {
            display = field.value;
        }
    }

    return display;
}

DivideBlock::ElementInfo ConvertElement(const GetTagInfo::RangeInfo &range) {
    const std::optional<std::string> display = FindDisplayOverride(range.fields);
    const bool has_display_override = display.has_value();
    const bool is_block = has_display_override ? IsBlockDisplayValue(*display)
                                               : DivideBlock::IsBlockTag(range.tag_name);

    return DivideBlock::ElementInfo{range.tag_name,
                                    range.value,
                                    range.raw,
                                    range.raw_begin,
                                    range.value_begin,
                                    range.value_end,
                                    range.raw_end,
                                    is_block,
                                    has_display_override,
                                    has_display_override ? NormalizeDisplayValue(*display)
                                                         : std::string{}};
}

} // namespace

DivideBlock::DivideBlock() {}

bool DivideBlock::Parse(const char *html) {
    if (html == nullptr) {
        Clear();
        error_ = "html input is null";

        return false;
    }

    return Parse(std::string_view(html));
}

bool DivideBlock::Parse(std::string_view html) {

    try {
        Clear();

        GetTagInfo parser;
        if (!parser.Parse(html)) {
            error_ = parser.GetError();
            if (error_.empty()) {
                error_ = "block division parse failed";
            }

            return false;
        }

        elements_.reserve(parser.GetRanges().size());
        block_elements_.reserve(parser.GetRanges().size());
        for (const GetTagInfo::RangeInfo &range : parser.GetRanges()) {
            ElementInfo element = ConvertElement(range);
            if (element.is_block) {
                block_elements_.push_back(element);
            }
            elements_.push_back(std::move(element));
        }

        return true;
    } catch (const std::exception &exception) {
        Clear();
        error_ = std::string("block division exception: ") + exception.what();

        return false;
    } catch (...) {
        Clear();
        error_ = "block division exception: unknown";

        return false;
    }
}

const std::vector<DivideBlock::ElementInfo> &DivideBlock::GetElements() const { return elements_; }

const std::vector<DivideBlock::ElementInfo> &DivideBlock::GetBlockElements() const {
    return block_elements_;
}

const std::string &DivideBlock::GetError() const { return error_; }

bool DivideBlock::IsBlockTag(std::string_view tag_name) {
    static constexpr std::array<std::string_view, 37> block_tags = {
        "address", "article", "aside",   "blockquote", "body",       "dd",     "details", "dialog",
        "div",     "dl",      "dt",      "fieldset",   "figcaption", "figure", "footer",  "form",
        "h1",      "h2",      "h3",      "h4",         "h5",         "h6",     "header",  "hgroup",
        "hr",      "html",    "li",      "main",       "menu",       "nav",    "ol",      "p",
        "pre",     "section", "summary", "table",      "ul"};

    const std::string normalized = ToLowerAscii(TrimAscii(tag_name));
    return std::find(block_tags.begin(), block_tags.end(), normalized) != block_tags.end();
}

bool DivideBlock::IsBlockElement(const GetTagInfo::RangeInfo &element) {
    return ConvertElement(element).is_block;
}

void DivideBlock::Clear() {
    elements_.clear();
    block_elements_.clear();
    error_.clear();
}

} // namespace ii::document::detail::blocks
