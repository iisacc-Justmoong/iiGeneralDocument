// Adapted from first-party iiXml/Elements/Doctype; standard C++ parser, UI
// bindings removed.
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace ii::document::detail::markup::Elements {

enum class DoctypeKind { XmlDeclaration, DoctypeDeclaration };

enum class DoctypeStatus {
    Matched,
    EmptyInput,
    NoTopDeclaration,
    MalformedXmlDeclaration,
    MalformedDoctypeDeclaration,
    ExceptionThrown
};

struct DoctypeMatch {
    DoctypeKind Kind;
    std::string Raw;
};

struct DoctypeResult {
    DoctypeStatus Status;
    std::optional<DoctypeMatch> Match;
    std::string Reason;
};

class Doctype {

  public:
    enum class Kind { XmlDeclaration, DoctypeDeclaration };

    explicit Doctype();

    [[nodiscard]] DoctypeResult MatchTop(std::string_view Input) const;
    [[nodiscard]] DoctypeResult MatchTopResult(std::string_view Input) const;
    [[nodiscard]] std::optional<DoctypeMatch> MatchTopMatch(std::string_view Input) const;
    [[nodiscard]] bool IsTopDoctype(std::string_view Input) const;
    [[nodiscard]] bool IsTopXmlDeclaration(std::string_view Input) const;
};

} // namespace ii::document::detail::markup::Elements
