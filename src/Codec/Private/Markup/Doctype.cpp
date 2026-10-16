// Adapted from first-party iiXml/Elements/Doctype; standard C++ parser, UI
// bindings removed.
#include "Doctype.h"

#include "Codec/Private/Markup/Diagnostics.h"

#include <cstddef>
#include <exception>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace {

constexpr std::string_view xml_declaration_start = "<?xml";
constexpr std::string_view xml_declaration_end = "?>";
constexpr std::string_view doctype_start = "<!Doctype";
constexpr std::string_view utf8_bom = "\xEF\xBB\xBF";

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

std::string_view trim_top(std::string_view input) {
    if (input.starts_with(utf8_bom)) {
        input.remove_prefix(utf8_bom.size());
    }

    while (!input.empty() && is_space(input.front())) {
        input.remove_prefix(1);
    }

    return input;
}

bool starts_with_token(std::string_view input, std::string_view token) {
    return input.size() >= token.size() && input.substr(0, token.size()) == token;
}

std::optional<std::size_t> find_xml_declaration_end(std::string_view input) {
    const std::size_t end = input.find(xml_declaration_end);
    if (end == std::string_view::npos) {
        return std::nullopt;
    }

    return end + xml_declaration_end.size();
}

std::optional<std::size_t> find_doctype_end(std::string_view input) {
    char quote = '\0';
    int subset_depth = 0;

    for (std::size_t index = 0; index < input.size(); ++index) {
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

        if (value == '[') {
            ++subset_depth;
            continue;
        }

        if (value == ']' && subset_depth > 0) {
            --subset_depth;
            continue;
        }

        if (value == '>' && subset_depth == 0) {
            return index + 1;
        }
    }

    return std::nullopt;
}

bool has_xml_declaration_body(std::string_view declaration) {
    if (declaration.size() <= xml_declaration_start.size() + xml_declaration_end.size()) {
        return false;
    }

    const char after_start = declaration[xml_declaration_start.size()];
    return is_space(after_start);
}

bool has_doctype_name(std::string_view declaration) {
    if (declaration.size() <= doctype_start.size() + 1) {
        return false;
    }

    std::size_t index = doctype_start.size();
    if (!is_space(declaration[index])) {
        return false;
    }

    while (index < declaration.size() && is_space(declaration[index])) {
        ++index;
    }

    if (index >= declaration.size() || !is_name_start(declaration[index])) {
        return false;
    }

    ++index;
    while (index < declaration.size() && is_name_char(declaration[index])) {
        ++index;
    }

    return index < declaration.size() &&
           (is_space(declaration[index]) || declaration[index] == '>');
}

} // namespace

namespace ii::document::detail::markup::Elements {

namespace {

DoctypeResult make_doctype_failure(DoctypeStatus status, std::string reason) {
    return DoctypeResult{status, std::nullopt, std::move(reason)};
}

DoctypeResult make_doctype_success(DoctypeMatch match) {
    return DoctypeResult{DoctypeStatus::Matched, std::move(match), "top declaration matched"};
}

} // namespace

Doctype::Doctype() {}

DoctypeResult Doctype::MatchTopResult(std::string_view input) const {

    try {
        input = trim_top(input);

        if (input.empty()) {

            return make_doctype_failure(
                DoctypeStatus::EmptyInput,
                "input is empty; expected XML declaration or Doctype declaration");
        }

        if (starts_with_token(input, xml_declaration_start)) {

            const std::optional<std::size_t> end = find_xml_declaration_end(input);
            if (!end.has_value()) {

                return make_doctype_failure(DoctypeStatus::MalformedXmlDeclaration,
                                            "XML declaration is not closed with ?>");
            }

            const std::string_view raw = input.substr(0, *end);
            if (!has_xml_declaration_body(raw)) {

                return make_doctype_failure(DoctypeStatus::MalformedXmlDeclaration,
                                            "XML declaration must contain a body after <?xml");
            }

            return make_doctype_success(
                DoctypeMatch{DoctypeKind::XmlDeclaration, std::string(raw)});
        }

        if (starts_with_token(input, doctype_start)) {

            const std::optional<std::size_t> end = find_doctype_end(input);
            if (!end.has_value()) {

                return make_doctype_failure(DoctypeStatus::MalformedDoctypeDeclaration,
                                            "Doctype declaration is not closed with >");
            }

            const std::string_view raw = input.substr(0, *end);
            if (!has_doctype_name(raw)) {

                return make_doctype_failure(DoctypeStatus::MalformedDoctypeDeclaration,
                                            "Doctype declaration is missing a valid root name");
            }

            return make_doctype_success(
                DoctypeMatch{DoctypeKind::DoctypeDeclaration, std::string(raw)});
        }

        return make_doctype_failure(DoctypeStatus::NoTopDeclaration,
                                    "top declaration is missing; expected XML "
                                    "declaration or Doctype declaration");
    } catch (const std::exception &exception) {

        return make_doctype_failure(DoctypeStatus::ExceptionThrown,
                                    std::string("Doctype match exception: ") + exception.what());
    } catch (...) {

        return make_doctype_failure(DoctypeStatus::ExceptionThrown,
                                    "Doctype match exception: unknown");
    }
}

DoctypeResult Doctype::MatchTop(std::string_view input) const {

    const DoctypeResult result = MatchTopResult(input);
    if (result.Match.has_value()) {

    } else {
    }
    return result;
}

std::optional<DoctypeMatch> Doctype::MatchTopMatch(std::string_view input) const {

    const DoctypeResult result = MatchTopResult(input);
    if (result.Match.has_value()) {

    } else {
    }
    return result.Match;
}

bool Doctype::IsTopDoctype(std::string_view input) const {

    const DoctypeResult matched = MatchTop(input);
    const bool result =
        matched.Match.has_value() && matched.Match->Kind == DoctypeKind::DoctypeDeclaration;

    return result;
}

bool Doctype::IsTopXmlDeclaration(std::string_view input) const {

    const DoctypeResult matched = MatchTop(input);
    const bool result =
        matched.Match.has_value() && matched.Match->Kind == DoctypeKind::XmlDeclaration;

    return result;
}

} // namespace ii::document::detail::markup::Elements
