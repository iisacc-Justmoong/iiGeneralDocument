// Adapted from first-party iiXml/Parser/TagParser; standard C++ parser, UI
// bindings removed.
#include "TagParser.h"

#include "Codec/Private/Markup/Diagnostics.h"
#include "Codec/Private/Markup/OpenTag.h"

#include <cstddef>
#include <exception>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

bool is_space(char value) {
    return value == ' ' || value == '\n' || value == '\r' || value == '\t';
}

std::string_view trim_outer(std::string_view input) {
    std::size_t begin = 0;
    while (begin < input.size() && is_space(input[begin])) {
        ++begin;
    }

    std::size_t end = input.size();
    while (end > begin && is_space(input[end - 1])) {
        --end;
    }

    return input.substr(begin, end - begin);
}

bool is_alpha(char value) {
    return ('a' <= value && value <= 'z') || ('A' <= value && value <= 'Z');
}

bool is_digit(char value) { return '0' <= value && value <= '9'; }

bool is_tag_start(char value) { return is_alpha(value) || value == '_'; }

bool is_tag_char(char value) {
    return is_tag_start(value) || is_digit(value) || value == '-' || value == '.' || value == ':';
}

bool is_valid_tag_name(std::string_view TagName) {
    if (TagName.empty() || !is_tag_start(TagName.front())) {
        return false;
    }

    for (char value : TagName.substr(1)) {
        if (!is_tag_char(value)) {
            return false;
        }
    }

    return true;
}

std::string_view read_opening_tag_name(std::string_view opening_tag) {
    opening_tag = trim_outer(opening_tag);
    std::size_t end = 0;
    while (end < opening_tag.size() && !is_space(opening_tag[end])) {
        ++end;
    }

    return opening_tag.substr(0, end);
}

} // namespace

namespace ii::document::detail::markup::Parser {

namespace {

struct tag_node_entry {
    TagNode Node;
    std::vector<std::size_t> Children;
};

bool contains_range(const TagRange &parent, const TagRange &child) {
    return parent.RawBegin < child.RawBegin && child.RawEnd <= parent.RawEnd;
}

std::optional<std::vector<TagField>> parse_fields(std::string_view input, const TagRange &range,
                                                  const char *) {

    if (range.ValueBegin <= range.RawBegin + 1 || range.ValueBegin > input.size()) {

        return std::nullopt;
    }

    const std::size_t close_bracket = range.ValueBegin - 1;
    if (close_bracket >= input.size() || input[range.RawBegin] != '<' ||
        input[close_bracket] != '>') {

        return std::nullopt;
    }

    const ii::document::detail::markup::Elements::InlineProperties properties;
    const std::optional<std::vector<ii::document::detail::markup::Elements::InlineProperty>>
        parsed = properties.Parse(input.substr(range.RawBegin, range.ValueBegin - range.RawBegin),
                                  range.RawBegin);
    if (!parsed.has_value()) {

        return std::nullopt;
    }

    std::vector<TagField> fields;
    fields.reserve(parsed->size());
    for (const ii::document::detail::markup::Elements::InlineProperty &property : *parsed) {
        fields.push_back(TagField{property.Name, property.NameBegin, property.NameEnd,
                                  property.HasValue, property.ValueBegin, property.ValueEnd,
                                  property.ValueType, property.TypeDeclared});
    }

    return fields;
}

std::optional<std::vector<TagNode>>
build_hierarchy(std::string_view input,
                std::vector<ii::document::detail::markup::Elements::OpenTagRange> &ranges,
                const char *scope) {
    std::vector<tag_node_entry> entries;
    entries.reserve(ranges.size());
    std::vector<std::size_t> roots;
    std::vector<std::size_t> active_nodes;

    for (ii::document::detail::markup::Elements::OpenTagRange &source : ranges) {

        TagRange range{std::move(source.TagName), source.RawBegin, source.ValueBegin,
                       source.ValueEnd, source.RawEnd};

        std::optional<std::vector<TagField>> fields = parse_fields(input, range, scope);
        if (!fields.has_value()) {
            return std::nullopt;
        }

        const std::size_t current_index = entries.size();
        entries.push_back(tag_node_entry{TagNode{std::move(range), std::move(*fields), {}}, {}});

        while (!active_nodes.empty() && entries[active_nodes.back()].Node.Range.RawEnd <=
                                            entries[current_index].Node.Range.RawBegin) {
            active_nodes.pop_back();
        }

        std::optional<std::size_t> parent_index;
        for (auto iterator = active_nodes.rbegin(); iterator != active_nodes.rend(); ++iterator) {
            if (contains_range(entries[*iterator].Node.Range, entries[current_index].Node.Range)) {
                parent_index = *iterator;
                break;
            }
        }

        if (parent_index.has_value()) {
            entries[*parent_index].Children.push_back(current_index);
        } else {
            roots.push_back(current_index);
        }

        active_nodes.push_back(current_index);
    }

    std::function<TagNode(std::size_t)> materialize = [&](std::size_t index) {
        TagNode node = std::move(entries[index].Node);
        node.Children.reserve(entries[index].Children.size());
        for (std::size_t child : entries[index].Children) {
            node.Children.push_back(materialize(child));
        }
        return node;
    };

    std::vector<TagNode> result;
    result.reserve(roots.size());
    for (std::size_t root : roots) {
        result.push_back(materialize(root));
    }

    return result;
}

ParserDiagnostic make_diagnostic(std::string_view input, std::string reason, std::size_t offset) {
    const ii::document::detail::markup::Diagnostics::SourcePosition position =
        ii::document::detail::markup::Diagnostics::LocateSourcePosition(input, offset);
    return ParserDiagnostic{
        std::move(reason), position.Offset, position.Line, position.Column,
        ii::document::detail::markup::Diagnostics::SourceContext(input, offset)};
}

ParserDiagnostic make_success_diagnostic(std::string reason) {
    return ParserDiagnostic{std::move(reason), 0, 1, 1, {}};
}

TagParseResult make_tag_parse_failure(TagParseStatus status, std::string_view input,
                                      std::string reason, std::size_t offset) {
    return TagParseResult{status, std::nullopt, make_diagnostic(input, std::move(reason), offset)};
}

TagTreeParseResult make_tag_tree_parse_failure(TagTreeParseStatus status, std::string_view input,
                                               std::string reason, std::size_t offset) {
    return TagTreeParseResult{status, std::nullopt,
                              make_diagnostic(input, std::move(reason), offset)};
}

TagDocumentResult MakeTagDocumentFailure(TagTreeParseStatus status, std::string_view input,
                                         std::string reason, std::size_t offset) {
    return TagDocumentResult{status, std::nullopt,
                             make_diagnostic(input, std::move(reason), offset)};
}

std::string_view CheckedSourceView(const std::string &source, std::size_t begin, std::size_t end,
                                   const char *) {

    if (begin > end || end > source.size()) {

        return {};
    }

    const std::string_view view(source.data() + begin, end - begin);

    return view;
}

TagParseResult parse_result(std::string_view input, const char *) {

    try {
        input = trim_outer(input);
        if (input.empty()) {
            const std::string reason = "input is empty";

            return make_tag_parse_failure(TagParseStatus::EmptyInput, input, reason, 0);
        }

        if (input.front() != '<') {
            const std::string reason = "missing opening bracket";

            return make_tag_parse_failure(TagParseStatus::MissingOpeningBracket, input, reason, 0);
        }

        const std::size_t opening_end = input.find('>');
        if (opening_end == std::string_view::npos) {
            const std::string reason = "opening tag not closed";

            return make_tag_parse_failure(TagParseStatus::OpeningTagNotClosed, input, reason, 0);
        }

        const std::string_view TagName = read_opening_tag_name(input.substr(1, opening_end - 1));
        if (!is_valid_tag_name(TagName)) {
            const std::string reason = "invalid tag name";

            return make_tag_parse_failure(TagParseStatus::InvalidTagName, input, reason, 1);
        }

        const std::string closing_tag = "</" + std::string(TagName) + ">";
        if (input.size() < opening_end + 1 + closing_tag.size()) {
            const std::string reason = "input shorter than closing tag";

            return make_tag_parse_failure(TagParseStatus::InputShorterThanClosingTag, input, reason,
                                          input.size());
        }

        const std::size_t closing_start = input.size() - closing_tag.size();
        if (input.compare(closing_start, closing_tag.size(), closing_tag) != 0) {
            const std::string reason = "closing tag mismatch";

            return make_tag_parse_failure(TagParseStatus::ClosingTagMismatch, input, reason,
                                          closing_start);
        }

        const std::size_t value_start = opening_end + 1;
        const std::size_t value_size = closing_start - value_start;
        TagValue token{std::string(TagName), std::string(input.substr(value_start, value_size)),
                       std::string(input)};

        return TagParseResult{TagParseStatus::Parsed, std::move(token),
                              make_success_diagnostic("tag parsed")};
    } catch (const std::exception &exception) {
        const std::string reason = std::string("tag parse exception: ") + exception.what();

        return make_tag_parse_failure(TagParseStatus::ExceptionThrown, input, reason, 0);
    } catch (...) {
        const std::string reason = "tag parse exception: unknown";

        return make_tag_parse_failure(TagParseStatus::ExceptionThrown, input, reason, 0);
    }
}

TagTreeParseResult parse_all_result(std::string_view input, const char *scope) {

    try {
        const ii::document::detail::markup::Elements::OpenTag open_tag;
        std::optional<std::vector<ii::document::detail::markup::Elements::OpenTagRange>> parsed =
            open_tag.ParseOpenTags(input);
        if (!parsed.has_value()) {
            const std::string reason = "open tag parser rejected input";

            return make_tag_tree_parse_failure(TagTreeParseStatus::OpenTagParserRejected, input,
                                               reason, 0);
        }

        std::optional<std::vector<TagNode>> result = build_hierarchy(input, *parsed, scope);
        if (!result.has_value()) {
            const std::string reason = "hierarchy build failed";

            return make_tag_tree_parse_failure(TagTreeParseStatus::HierarchyBuildFailed, input,
                                               reason, 0);
        }

        return TagTreeParseResult{TagTreeParseStatus::Parsed, std::move(result),
                                  make_success_diagnostic("tag tree parsed")};
    } catch (const std::exception &exception) {
        const std::string reason = std::string("tag tree parse exception: ") + exception.what();

        return make_tag_tree_parse_failure(TagTreeParseStatus::ExceptionThrown, input, reason, 0);
    } catch (...) {
        const std::string reason = "tag tree parse exception: unknown";

        return make_tag_tree_parse_failure(TagTreeParseStatus::ExceptionThrown, input, reason, 0);
    }
}

TagDocumentResult ParseAllDocumentResultInternal(std::string_view input, const char *scope) {

    try {
        const std::string RangeScope = std::string(scope) + ".range";
        TagTreeParseResult Parsed = parse_all_result(input, RangeScope.c_str());
        if (Parsed.Status != TagTreeParseStatus::Parsed || !Parsed.Nodes.has_value()) {
            const TagTreeParseStatus Status = Parsed.Status == TagTreeParseStatus::Parsed
                                                  ? TagTreeParseStatus::HierarchyBuildFailed
                                                  : Parsed.Status;
            const std::string Reason = Parsed.Diagnostic.Reason.empty()
                                           ? "tag document parse failed"
                                           : Parsed.Diagnostic.Reason;

            if (Parsed.Diagnostic.Reason.empty()) {
                return MakeTagDocumentFailure(Status, input, Reason, 0);
            }

            return TagDocumentResult{Status, std::nullopt, std::move(Parsed.Diagnostic)};
        }

        TagDocument Document{std::string(input), std::move(*Parsed.Nodes)};

        return TagDocumentResult{TagTreeParseStatus::Parsed, std::move(Document),
                                 make_success_diagnostic("tag document parsed")};
    } catch (const std::exception &exception) {
        const std::string reason = std::string("tag document parse exception: ") + exception.what();

        return MakeTagDocumentFailure(TagTreeParseStatus::ExceptionThrown, input, reason, 0);
    } catch (...) {
        const std::string reason = "tag document parse exception: unknown";

        return MakeTagDocumentFailure(TagTreeParseStatus::ExceptionThrown, input, reason, 0);
    }
}

} // namespace

std::string_view TagDocument::RawView(const TagNode &Node) const {
    return CheckedSourceView(Source, Node.Range.RawBegin, Node.Range.RawEnd,
                             "ii::document::detail::markup::Parser::TagDocument::RawView");
}

std::string_view TagDocument::ValueView(const TagNode &Node) const {
    return CheckedSourceView(Source, Node.Range.ValueBegin, Node.Range.ValueEnd,
                             "ii::document::detail::markup::Parser::TagDocument::ValueView");
}

std::string_view TagDocument::FieldNameView(const TagField &Field) const {
    return CheckedSourceView(Source, Field.NameBegin, Field.NameEnd,
                             "ii::document::detail::markup::Parser::TagDocument::FieldNameView");
}

std::string_view TagDocument::FieldValueView(const TagField &Field) const {

    if (!Field.HasValue) {

        return {};
    }

    return CheckedSourceView(Source, Field.ValueBegin, Field.ValueEnd,
                             "ii::document::detail::markup::Parser::TagDocument::FieldValueView");
}

TagParser::TagParser() {}

std::optional<TagValue> TagParser::Parse(std::string_view input) const {
    return parse_result(input, "ii::document::detail::markup::Parser::TagParser::Parse").Token;
}

TagParseResult TagParser::ParseResult(std::string_view input) const {
    return parse_result(input, "ii::document::detail::markup::Parser::TagParser::ParseResult");
}

std::optional<std::vector<TagNode>> TagParser::ParseAll(std::string_view input) const {
    return parse_all_result(input, "ii::document::detail::markup::Parser::TagParser::ParseAll")
        .Nodes;
}

TagTreeParseResult TagParser::ParseAllResult(std::string_view input) const {
    return parse_all_result(input,
                            "ii::document::detail::markup::Parser::TagParser::ParseAllResult");
}

std::optional<TagDocument> TagParser::ParseAllDocument(std::string_view input) const {
    return ParseAllDocumentResultInternal(input, "ii::document::detail::markup::Parser::"
                                                 "TagParser::ParseAllDocument")
        .Document;
}

TagDocumentResult TagParser::ParseAllDocumentResult(std::string_view input) const {
    return ParseAllDocumentResultInternal(input, "ii::document::detail::markup::Parser::"
                                                 "TagParser::ParseAllDocumentResult");
}

} // namespace ii::document::detail::markup::Parser
