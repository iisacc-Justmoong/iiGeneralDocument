// Adapted from first-party iiXml/Elements/OpenTag; standard C++ parser, UI
// bindings removed.
#include "OpenTag.h"

#include "Codec/Private/Markup/Diagnostics.h"
#include "Codec/Private/Markup/Doctype.h"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <iterator>
#include <list>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ii::document::detail::markup::Elements {

namespace {

struct active_open_tag {
    std::string Name;
    std::size_t OpenBegin;
    std::size_t ValueBegin;
    std::size_t Sequence;
};

struct indexed_open_tag_range {
    std::size_t Sequence;
    OpenTagRange Range;
};

using active_open_tag_list = std::list<active_open_tag>;
using active_open_tag_iterator = active_open_tag_list::iterator;
using active_open_tag_index =
    std::unordered_map<std::string, std::vector<active_open_tag_iterator>>;

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

bool starts_with_at(std::string_view input, std::size_t index, std::string_view token) {
    return index + token.size() <= input.size() && input.substr(index, token.size()) == token;
}

std::optional<std::size_t> find_token_end(std::string_view input, std::size_t begin,
                                          std::string_view token) {
    const std::size_t end = input.find(token, begin);
    if (end == std::string_view::npos) {
        return std::nullopt;
    }

    return end + token.size();
}

std::optional<std::size_t> find_markup_end(std::string_view input, std::size_t begin) {
    char quote = '\0';

    for (std::size_t index = begin; index < input.size(); ++index) {
        const char value = input[index];

        if (quote != '\0') {
            if (value == quote) {
                quote = '\0';
            }
            continue;
        }

        if (value == '"' || value == '\'') {
            quote = value;
            continue;
        }

        if (value == '>') {
            return index;
        }
    }

    return std::nullopt;
}

std::optional<std::string> read_tag_name(std::string_view markup, std::size_t begin) {
    if (begin >= markup.size() || !is_name_start(markup[begin])) {
        return std::nullopt;
    }

    std::size_t end = begin + 1;
    while (end < markup.size() && is_name_char(markup[end])) {
        ++end;
    }

    return std::string(markup.substr(begin, end - begin));
}

bool is_self_closing_markup(std::string_view markup) {
    if (markup.size() < 2 || markup.back() != '>') {
        return false;
    }

    std::size_t index = markup.size() - 1;
    while (index > 0 && is_space(markup[index - 1])) {
        --index;
    }

    return index > 0 && markup[index - 1] == '/';
}

} // namespace

OpenTag::OpenTag() {}

bool OpenTag::CloseOpenTag(std::vector<std::string> &open_tags,
                           std::string_view closing_tag_name) const {

    try {
        if (closing_tag_name.empty()) {

            return false;
        }

        const auto matched =
            std::find_if(open_tags.rbegin(), open_tags.rend(),
                         [&](const std::string &TagName) { return TagName == closing_tag_name; });
        if (matched == open_tags.rend()) {

            return false;
        }

        open_tags.erase(std::next(matched).base());

        return true;
    } catch (const std::exception &exception) {

        return false;
    } catch (...) {

        return false;
    }
}

std::optional<std::vector<OpenTagRange>> OpenTag::ParseOpenTags(std::string_view input) const {

    try {
        active_open_tag_list active_tags;
        active_open_tag_index active_tags_by_name;
        std::vector<indexed_open_tag_range> parsed_tags;
        std::size_t sequence = 0;

        for (std::size_t index = 0; index < input.size();) {
            const std::size_t tag_start = input.find('<', index);
            if (tag_start == std::string_view::npos) {
                break;
            }

            if (starts_with_at(input, tag_start, "<!--")) {
                const std::optional<std::size_t> end = find_token_end(input, tag_start + 4, "-->");
                if (!end.has_value()) {

                    return std::nullopt;
                }
                index = *end;
                continue;
            }

            if (starts_with_at(input, tag_start, "<![CDATA[")) {
                const std::optional<std::size_t> end = find_token_end(input, tag_start + 9, "]]>");
                if (!end.has_value()) {

                    return std::nullopt;
                }
                index = *end;
                continue;
            }

            if (starts_with_at(input, tag_start, "<?")) {
                const std::optional<std::size_t> end = find_token_end(input, tag_start + 2, "?>");
                if (!end.has_value()) {

                    return std::nullopt;
                }
                index = *end;
                continue;
            }

            if (starts_with_at(input, tag_start, "<!Doctype")) {
                const Doctype doctype;
                const DoctypeResult matched = doctype.MatchTop(input.substr(tag_start));
                if (!matched.Match.has_value()) {

                    return std::nullopt;
                }
                index = tag_start + matched.Match->Raw.size();
                continue;
            }

            const std::optional<std::size_t> tag_end = find_markup_end(input, tag_start + 1);
            if (!tag_end.has_value()) {

                return std::nullopt;
            }

            const std::string_view markup = input.substr(tag_start, *tag_end - tag_start + 1);
            if (markup.size() < 3) {

                return std::nullopt;
            }

            if (markup[1] == '/') {
                const std::optional<std::string> TagName = read_tag_name(markup, 2);
                if (!TagName.has_value()) {

                    return std::nullopt;
                }

                const auto indexed_tags = active_tags_by_name.find(*TagName);
                if (indexed_tags == active_tags_by_name.end() || indexed_tags->second.empty()) {

                    return std::nullopt;
                }

                active_open_tag_iterator matched = indexed_tags->second.back();
                active_open_tag closed = std::move(*matched);
                active_tags.erase(matched);
                indexed_tags->second.pop_back();
                if (indexed_tags->second.empty()) {
                    active_tags_by_name.erase(indexed_tags);
                }

                const std::size_t close_end = *tag_end + 1;
                parsed_tags.push_back(indexed_open_tag_range{
                    closed.Sequence, OpenTagRange{std::move(closed.Name), closed.OpenBegin,
                                                  closed.ValueBegin, tag_start, close_end}});
            } else {
                const std::optional<std::string> TagName = read_tag_name(markup, 1);
                if (!TagName.has_value()) {

                    return std::nullopt;
                }

                if (is_self_closing_markup(markup)) {

                    parsed_tags.push_back(indexed_open_tag_range{
                        sequence, OpenTagRange{*TagName, tag_start, *tag_end + 1, *tag_end + 1,
                                               *tag_end + 1}});
                    ++sequence;
                } else {

                    active_tags.push_back(
                        active_open_tag{*TagName, tag_start, *tag_end + 1, sequence});
                    active_open_tag_iterator opened = std::prev(active_tags.end());
                    active_tags_by_name[opened->Name].push_back(opened);
                    ++sequence;
                }
            }

            index = *tag_end + 1;
        }

        if (!active_tags.empty()) {

            return std::nullopt;
        }

        std::sort(parsed_tags.begin(), parsed_tags.end(),
                  [](const indexed_open_tag_range &left, const indexed_open_tag_range &right) {
                      return left.Sequence < right.Sequence;
                  });

        std::vector<OpenTagRange> result;
        result.reserve(parsed_tags.size());
        for (indexed_open_tag_range &parsed : parsed_tags) {
            result.push_back(std::move(parsed.Range));
        }

        return result;
    } catch (const std::exception &exception) {

        return std::nullopt;
    } catch (...) {

        return std::nullopt;
    }
}

} // namespace ii::document::detail::markup::Elements
