// Adapted from first-party iiXml/Parser/TagParser; standard C++ parser, UI
// bindings removed.
#ifndef IIXML_PARSER_TAG_PARSER_H
#define IIXML_PARSER_TAG_PARSER_H

#include "Codec/Private/Markup/InlineProperties.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ii::document::detail::markup::Parser {

struct TagValue {
    std::string TagName;
    std::string Value;
    std::string Raw;
};

struct TagRange {
    std::string TagName;
    std::size_t RawBegin;
    std::size_t ValueBegin;
    std::size_t ValueEnd;
    std::size_t RawEnd;
};

struct TagField {
    std::string Name;
    std::size_t NameBegin;
    std::size_t NameEnd;
    bool HasValue;
    std::size_t ValueBegin;
    std::size_t ValueEnd;
    ii::document::detail::markup::Elements::InlinePropertyType ValueType;
    bool TypeDeclared;
};

struct TagNode {
    TagRange Range;
    std::vector<TagField> Fields;
    std::vector<TagNode> Children;
};

struct TagDocument {
    std::string Source;
    std::vector<TagNode> Nodes;

    [[nodiscard]] std::string_view RawView(const TagNode &Node) const;
    [[nodiscard]] std::string_view ValueView(const TagNode &Node) const;
    [[nodiscard]] std::string_view FieldNameView(const TagField &Field) const;
    [[nodiscard]] std::string_view FieldValueView(const TagField &Field) const;
};

enum class TagParseStatus {
    Parsed,
    EmptyInput,
    MissingOpeningBracket,
    OpeningTagNotClosed,
    InvalidTagName,
    InputShorterThanClosingTag,
    ClosingTagMismatch,
    ExceptionThrown
};

enum class TagTreeParseStatus {
    Parsed,
    OpenTagParserRejected,
    HierarchyBuildFailed,
    ExceptionThrown
};

struct ParserDiagnostic {
    std::string Reason;
    std::size_t Offset;
    std::size_t Line;
    std::size_t Column;
    std::string Context;
};

struct TagParseResult {
    TagParseStatus Status;
    std::optional<TagValue> Token;
    ParserDiagnostic Diagnostic;
};

struct TagTreeParseResult {
    TagTreeParseStatus Status;
    std::optional<std::vector<TagNode>> Nodes;
    ParserDiagnostic Diagnostic;
};

struct TagDocumentResult {
    TagTreeParseStatus Status;
    std::optional<TagDocument> Document;
    ParserDiagnostic Diagnostic;
};

class TagParser {

  public:
    explicit TagParser();

    [[nodiscard]] std::optional<TagValue> Parse(std::string_view Input) const;
    [[nodiscard]] TagParseResult ParseResult(std::string_view Input) const;
    [[nodiscard]] std::optional<std::vector<TagNode>> ParseAll(std::string_view Input) const;
    [[nodiscard]] TagTreeParseResult ParseAllResult(std::string_view Input) const;
    [[nodiscard]] std::optional<TagDocument> ParseAllDocument(std::string_view Input) const;
    [[nodiscard]] TagDocumentResult ParseAllDocumentResult(std::string_view Input) const;
};

} // namespace ii::document::detail::markup::Parser

#endif // IIXML_PARSER_TAG_PARSER_H
