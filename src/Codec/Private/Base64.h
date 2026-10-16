#pragma once
#include "Core/Diagnostic.h"
#include <string>
#include <string_view>
namespace ii::document::detail {
inline std::string encodeBase64(std::string_view source) {
    constexpr std::string_view alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    result.reserve((source.size() + 2) / 3 * 4);
    for (std::size_t i = 0; i < source.size(); i += 3) {
        unsigned a = static_cast<unsigned char>(source[i]);
        unsigned b = i + 1 < source.size() ? static_cast<unsigned char>(source[i + 1]) : 0U;
        unsigned c = i + 2 < source.size() ? static_cast<unsigned char>(source[i + 2]) : 0U;
        result += alphabet[a >> 2];
        result += alphabet[((a & 3) << 4) | (b >> 4)];
        result += i + 1 < source.size() ? alphabet[((b & 15) << 2) | (c >> 6)] : '=';
        result += i + 2 < source.size() ? alphabet[c & 63] : '=';
    }
    return result;
}
inline std::string decodeBase64(std::string_view source) {
    constexpr std::string_view alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    if (source.size() % 4)
        throw DocumentError("Invalid Base64 length");
    std::string result;
    result.reserve(source.size() / 4 * 3);
    for (std::size_t i = 0; i < source.size(); i += 4) {
        unsigned bits = 0;
        int padding = 0;
        for (std::size_t n = 0; n < 4; ++n) {
            auto c = source[i + n];
            if (c == '=') {
                ++padding;
                if (i + 4 != source.size() || n < 2)
                    throw DocumentError("Invalid Base64 padding");
                bits <<= 6;
            } else {
                auto v = alphabet.find(c);
                if (padding || v == alphabet.npos)
                    throw DocumentError("Invalid Base64 digit");
                bits = (bits << 6) | static_cast<unsigned>(v);
            }
        }
        result += static_cast<char>(bits >> 16);
        if (padding < 2)
            result += static_cast<char>(bits >> 8);
        if (!padding)
            result += static_cast<char>(bits);
    }
    if (encodeBase64(result) != source)
        throw DocumentError("Noncanonical Base64");
    return result;
}
} // namespace ii::document::detail
