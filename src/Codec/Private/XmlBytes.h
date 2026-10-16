#pragma once
#include "Codec/Private/Text.h"
#include "Core/Diagnostic.h"
#include <regex>
namespace ii::document::detail {
inline bool isXmlScalar(char32_t c) {
    return c == 9 || c == 10 || c == 13 || (c >= 32 && c <= 0xd7ff) ||
           (c >= 0xe000 && c <= 0xfffd) || (c >= 0x10000 && c <= 0x10ffff);
}
inline void validateXmlText(const Text &text) {
    for (auto c : text)
        if (!isXmlScalar(c))
            throw DocumentError("Invalid XML character");
}
inline std::string xmlUtf8(std::string_view bytes) {
    auto b = [&](std::size_t i) { return static_cast<unsigned char>(bytes[i]); };
    int width = 1;
    bool little = false;
    std::size_t offset = 0;
    if (bytes.size() >= 4 && b(0) == 0 && b(1) == 0 && b(2) == 0xfe && b(3) == 0xff) {
        width = 4;
        offset = 4;
    } else if (bytes.size() >= 4 && b(0) == 0xff && b(1) == 0xfe && b(2) == 0 && b(3) == 0) {
        width = 4;
        little = true;
        offset = 4;
    } else if (bytes.size() >= 2 && b(0) == 0xfe && b(1) == 0xff) {
        width = 2;
        offset = 2;
    } else if (bytes.size() >= 2 && b(0) == 0xff && b(1) == 0xfe) {
        width = 2;
        little = true;
        offset = 2;
    } else if (bytes.size() >= 4 && b(0) == 0 && b(1) == 0 && b(2) == 0 && b(3) == '<')
        width = 4;
    else if (bytes.size() >= 4 && b(0) == '<' && b(1) == 0 && b(2) == 0 && b(3) == 0) {
        width = 4;
        little = true;
    } else if (bytes.size() >= 2 && b(0) == 0 && b(1) == '<')
        width = 2;
    else if (bytes.size() >= 2 && b(0) == '<' && b(1) == 0) {
        width = 2;
        little = true;
    } else if (bytes.starts_with("\xef\xbb\xbf"))
        offset = 3;
    std::u32string points;
    if (width == 1) {
        auto raw = bytes.substr(offset);
        bool latin = false;
        if (raw.starts_with("<?xml")) {
            auto end = raw.find("?>");
            if (end != raw.npos && end < 1024) {
                std::string declaration(raw.substr(0, end));
                latin = std::regex_search(
                    declaration, std::regex("encoding\\s*=\\s*['\"](?:ISO-8859-1|latin1)['\"]",
                                            std::regex::icase));
            }
        }
        if (latin)
            for (char c : raw)
                points += static_cast<char32_t>(static_cast<unsigned char>(c));
        else
            points = decodeUtf8(raw);
    } else {
        auto unitWidth = static_cast<std::size_t>(width);
        if ((bytes.size() - offset) % unitWidth)
            throw DocumentError("Truncated XML Unicode encoding");
        auto unit = [&](std::size_t i) {
            std::uint32_t c = 0;
            for (std::size_t n = 0; n < unitWidth; ++n)
                c |= static_cast<std::uint32_t>(b(i + n)) << (8 * (little ? n : unitWidth - n - 1));
            return c;
        };
        for (std::size_t i = offset; i < bytes.size(); i += unitWidth) {
            auto c = unit(i);
            if (width == 2 && c >= 0xd800 && c <= 0xdbff) {
                if (i + 4 > bytes.size())
                    throw DocumentError("Truncated XML surrogate");
                auto low = unit(i + 2);
                if (low < 0xdc00 || low > 0xdfff)
                    throw DocumentError("Invalid XML surrogate");
                c = 0x10000 + ((c - 0xd800) << 10) + (low - 0xdc00);
                i += 2;
            }
            if (c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff))
                throw DocumentError("Invalid XML Unicode scalar");
            points += static_cast<char32_t>(c);
        }
    }
    for (auto c : points)
        if (!isXmlScalar(c))
            throw DocumentError("Invalid XML character");
    return encodeUtf8(points);
}
inline void validateXmlEntities(std::string_view source) {
    for (std::size_t i = 0; i < source.size();) {
        auto rest = source.substr(i);
        std::string_view end;
        if (rest.starts_with("<!--"))
            end = "-->";
        else if (rest.starts_with("<![CDATA["))
            end = "]]>";
        else if (rest.starts_with("<?"))
            end = "?>";
        if (!end.empty()) {
            auto at = source.find(end, i + 2);
            if (at == source.npos)
                throw DocumentError("Unterminated XML markup");
            i = at + end.size();
            continue;
        }
        if (rest.starts_with("<!DOCTYPE")) {
            int depth = 0;
            char quote = 0;
            bool closed = false;
            for (i += 9; i < source.size(); ++i) {
                char c = source[i];
                if (quote) {
                    if (c == quote)
                        quote = 0;
                } else if (c == '\'' || c == '\"')
                    quote = c;
                else if (c == '[')
                    ++depth;
                else if (c == ']')
                    --depth;
                else if (c == '>' && depth == 0) {
                    ++i;
                    closed = true;
                    break;
                }
            }
            if (!closed)
                throw DocumentError("Unterminated XML doctype");
            continue;
        }
        if (source[i] != '&') {
            ++i;
            continue;
        }
        auto at = source.find(';', i + 1);
        if (at == source.npos || at - i > 32)
            throw DocumentError("Invalid XML entity");
        auto entity = source.substr(i + 1, at - i - 1);
        if (entity != "amp" && entity != "lt" && entity != "gt" && entity != "apos" &&
            entity != "quot") {
            if (!entity.starts_with('#'))
                throw DocumentError("Unsupported XML entity");
            entity.remove_prefix(1);
            int base = 10;
            if (entity.starts_with('x')) {
                entity.remove_prefix(1);
                base = 16;
            }
            std::uint32_t c = 0;
            auto parsed = std::from_chars(entity.data(), entity.data() + entity.size(), c, base);
            if (entity.empty() || parsed.ec != std::errc{} ||
                parsed.ptr != entity.data() + entity.size() ||
                !isXmlScalar(static_cast<char32_t>(c)))
                throw DocumentError("Invalid XML character reference");
        }
        i = at + 1;
    }
}
} // namespace ii::document::detail
