// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ii::document::detail {
inline bool validPdfText(std::string_view value) {
    if (value.size() > (1U << 20U))
        return false;
    for (std::size_t i = 0; i < value.size();) {
        const auto b = static_cast<unsigned char>(value[i]);
        if (b == 0) return false;
        if (b < 0x80U) { ++i; continue; }
        std::size_t count{};
        std::uint32_t scalar{};
        std::uint32_t minimum{};
        if (b >= 0xC2U && b <= 0xDFU) { count = 2; scalar = b & 0x1FU; minimum = 0x80U; }
        else if (b >= 0xE0U && b <= 0xEFU) { count = 3; scalar = b & 0x0FU; minimum = 0x800U; }
        else if (b >= 0xF0U && b <= 0xF4U) { count = 4; scalar = b & 0x07U; minimum = 0x10000U; }
        else return false;
        if (count > value.size() - i) return false;
        for (std::size_t n = 1; n < count; ++n) {
            const auto tail = static_cast<unsigned char>(value[i + n]);
            if ((tail & 0xC0U) != 0x80U) return false;
            scalar = (scalar << 6U) | (tail & 0x3FU);
        }
        if (scalar < minimum || scalar > 0x10FFFFU || (scalar >= 0xD800U && scalar <= 0xDFFFU))
            return false;
        i += count;
    }
    return true;
}
}
