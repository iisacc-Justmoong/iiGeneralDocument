#pragma once
#include <algorithm>
#include <charconv>
#include <cmath>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace ii::document::detail {
class ByteBuffer {
    std::string bytes_;

  public:
    ByteBuffer() = default;
    ByteBuffer(std::string bytes) : bytes_(std::move(bytes)) {}
    ByteBuffer(std::string_view bytes) : bytes_(bytes) {}
    ByteBuffer(const char *bytes) : bytes_(bytes) {}
    ByteBuffer(const char *bytes, std::ptrdiff_t count)
        : bytes_(bytes, static_cast<std::size_t>(count)) {}
    ByteBuffer(std::ptrdiff_t count, char value) : bytes_(static_cast<std::size_t>(count), value) {}
    static ByteBuffer fromStdString(const std::string &value) { return value; }
    [[nodiscard]] std::string toStdString() const { return bytes_; }
    [[nodiscard]] const char *constData() const noexcept { return bytes_.data(); }
    [[nodiscard]] char *data() noexcept { return bytes_.data(); }
    [[nodiscard]] const char *data() const noexcept { return bytes_.data(); }
    [[nodiscard]] std::ptrdiff_t size() const noexcept {
        return static_cast<std::ptrdiff_t>(bytes_.size());
    }
    [[nodiscard]] bool isEmpty() const noexcept { return bytes_.empty(); }
    void resize(std::ptrdiff_t value) { bytes_.resize(static_cast<std::size_t>(value)); }
    void reserve(std::ptrdiff_t value) { bytes_.reserve(static_cast<std::size_t>(value)); }
    void clear() noexcept { bytes_.clear(); }
    [[nodiscard]] char at(std::ptrdiff_t index) const {
        return bytes_.at(static_cast<std::size_t>(index));
    }
    char &operator[](std::ptrdiff_t index) { return bytes_[static_cast<std::size_t>(index)]; }
    char operator[](std::ptrdiff_t index) const { return bytes_[static_cast<std::size_t>(index)]; }
    [[nodiscard]] auto begin() const { return bytes_.begin(); }
    [[nodiscard]] auto end() const { return bytes_.end(); }
    [[nodiscard]] ByteBuffer mid(std::ptrdiff_t offset, std::ptrdiff_t count = -1) const {
        return bytes_.substr(static_cast<std::size_t>(offset),
                             count < 0 ? std::string::npos : static_cast<std::size_t>(count));
    }
    [[nodiscard]] ByteBuffer left(std::ptrdiff_t count) const { return mid(0, count); }
    [[nodiscard]] bool startsWith(std::string_view value) const {
        return bytes_.starts_with(value);
    }
    [[nodiscard]] bool contains(std::string_view value) const {
        return bytes_.find(value) != std::string::npos;
    }
    ByteBuffer &append(const char *value, std::ptrdiff_t size) {
        bytes_.append(value, static_cast<std::size_t>(size));
        return *this;
    }
    ByteBuffer &operator+=(const ByteBuffer &value) {
        bytes_ += value.bytes_;
        return *this;
    }
    operator std::string_view() const { return bytes_; }
    friend bool operator==(const ByteBuffer &, const ByteBuffer &) = default;
};
inline std::u32string decodeUtf8(std::string_view input) {
    std::u32string value;
    for (std::size_t i = 0; i < input.size();) {
        auto b = static_cast<unsigned char>(input[i]);
        std::size_t count{};
        char32_t point{}, minimum{};
        if (b < 0x80) {
            count = 1;
            point = b;
        } else if (b >= 0xc2 && b <= 0xdf) {
            count = 2;
            point = b & 31;
            minimum = 0x80;
        } else if (b >= 0xe0 && b <= 0xef) {
            count = 3;
            point = b & 15;
            minimum = 0x800;
        } else if (b >= 0xf0 && b <= 0xf4) {
            count = 4;
            point = b & 7;
            minimum = 0x10000;
        } else
            throw std::invalid_argument("Invalid UTF-8");
        if (count > input.size() - i)
            throw std::invalid_argument("Truncated UTF-8");
        for (std::size_t n = 1; n < count; ++n) {
            auto tail = static_cast<unsigned char>(input[i + n]);
            if ((tail & 0xc0) != 0x80)
                throw std::invalid_argument("Invalid UTF-8 continuation");
            point = (point << 6) | (tail & 63);
        }
        if (point < minimum || point > 0x10ffff || (point >= 0xd800 && point <= 0xdfff))
            throw std::invalid_argument("Invalid Unicode scalar");
        value.push_back(point);
        i += count;
    }
    return value;
}
inline std::string encodeUtf8(std::u32string_view points) {
    std::string value;
    for (auto c : points) {
        if (c < 0x80)
            value += static_cast<char>(c);
        else if (c < 0x800) {
            value += static_cast<char>(0xc0 | (c >> 6));
            value += static_cast<char>(0x80 | (c & 63));
        } else if (c < 0x10000) {
            value += static_cast<char>(0xe0 | (c >> 12));
            value += static_cast<char>(0x80 | ((c >> 6) & 63));
            value += static_cast<char>(0x80 | (c & 63));
        } else {
            value += static_cast<char>(0xf0 | (c >> 18));
            value += static_cast<char>(0x80 | ((c >> 12) & 63));
            value += static_cast<char>(0x80 | ((c >> 6) & 63));
            value += static_cast<char>(0x80 | (c & 63));
        }
    }
    return value;
}
class Text {
    std::u32string points_;
    bool present_{};

  public:
    Text() = default;
    Text(const char *value) : points_(decodeUtf8(value)), present_(true) {};
    Text(std::string value) : points_(decodeUtf8(value)), present_(true) {};
    Text(std::string_view value) : points_(decodeUtf8(value)), present_(true) {};
    explicit Text(std::u32string value) : points_(std::move(value)), present_(true) {}
    Text(std::ptrdiff_t count, char32_t value)
        : points_(static_cast<std::size_t>(count), value), present_(true) {}
    Text(char32_t value) : points_(1, value), present_(true) {}
    [[nodiscard]] bool isNull() const noexcept { return !present_; }
    static Text fromLatin1(const char *value, std::ptrdiff_t count) {
        std::u32string points;
        for (std::ptrdiff_t i = 0; i < count; ++i)
            points.push_back(static_cast<unsigned char>(value[i]));
        return Text(std::move(points));
    }
    static Text fromLatin1(const char *value) {
        std::u32string points;
        for (auto *p = value; *p; ++p)
            points.push_back(static_cast<unsigned char>(*p));
        return Text(std::move(points));
    }
    static Text fromUtf8(const std::string &value) { return value; }
    static Text fromUtf8(const ByteBuffer &value) { return value.toStdString(); }
    static Text fromUtf8(const char *value, std::ptrdiff_t count) {
        return std::string_view(value, static_cast<std::size_t>(count));
    }
    static Text fromStdString(const std::string &value) { return value; }
    template <class T> static Text number(T value, char format = 'g', int precision = 6) {
        char buffer[128];
        std::to_chars_result result;
        if constexpr (std::is_integral_v<T>)
            result = std::to_chars(buffer, buffer + sizeof(buffer), value);
        else
            result = std::to_chars(
                buffer, buffer + sizeof(buffer), value,
                format == 'f' ? std::chars_format::fixed : std::chars_format::general, precision);
        if (result.ec != std::errc{})
            throw std::invalid_argument("Invalid numeric text");
        return std::string(buffer, result.ptr);
    }
    [[nodiscard]] ByteBuffer toUtf8() const { return encodeUtf8(points_); }
    [[nodiscard]] std::string toStdString() const { return encodeUtf8(points_); }
    [[nodiscard]] Text toString() const { return *this; }
    [[nodiscard]] std::ptrdiff_t size() const {
        return static_cast<std::ptrdiff_t>(points_.size());
    }
    [[nodiscard]] bool isEmpty() const { return points_.empty(); }
    [[nodiscard]] char32_t front() const { return points_.front(); }
    [[nodiscard]] char32_t back() const { return points_.back(); }
    [[nodiscard]] char32_t operator[](std::ptrdiff_t index) const {
        return points_[static_cast<std::size_t>(index)];
    }
    [[nodiscard]] auto begin() const { return points_.begin(); }
    [[nodiscard]] auto end() const { return points_.end(); }
    void clear() { points_.clear(); }
    Text &operator+=(char32_t value) {
        points_ += value;
        return *this;
    }
    Text &operator+=(const Text &value) {
        points_ += value.points_;
        return *this;
    }
    Text &append(const Text &value) { return *this += value; }
    [[nodiscard]] Text mid(std::ptrdiff_t offset, std::ptrdiff_t count = -1) const {
        return Text(
            points_.substr(static_cast<std::size_t>(offset),
                           count < 0 ? std::u32string::npos : static_cast<std::size_t>(count)));
    }
    void remove(std::ptrdiff_t offset, std::ptrdiff_t count) {
        points_.erase(static_cast<std::size_t>(offset), static_cast<std::size_t>(count));
    }
    void chop(std::ptrdiff_t count) {
        points_.resize(points_.size() - static_cast<std::size_t>(count));
    }
    [[nodiscard]] bool contains(const Text &value) const {
        return points_.find(value.points_) != std::u32string::npos;
    }
    [[nodiscard]] bool contains(char32_t value) const {
        return points_.find(value) != std::u32string::npos;
    }
    [[nodiscard]] bool startsWith(char32_t value) const {
        return !points_.empty() && points_.front() == value;
    }
    [[nodiscard]] bool startsWith(const Text &value) const {
        return points_.starts_with(value.points_);
    }
    [[nodiscard]] bool endsWith(char32_t value) const {
        return !points_.empty() && points_.back() == value;
    }
    [[nodiscard]] bool endsWith(const Text &value) const {
        return points_.ends_with(value.points_);
    }
    Text &replace(char32_t from, char32_t to) {
        std::replace(points_.begin(), points_.end(), from, to);
        return *this;
    }
    [[nodiscard]] Text trimmed() const {
        auto space = [](char32_t c) {
            return c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '\v' || c == '\f' ||
                   c == 0xa0;
        };
        std::size_t first = 0, last = points_.size();
        while (first < last && space(points_[first]))
            ++first;
        while (last > first && space(points_[last - 1]))
            --last;
        return Text(points_.substr(first, last - first));
    }
    [[nodiscard]] Text toLower() const {
        auto value = points_;
        for (auto &c : value)
            if (c >= 'A' && c <= 'Z')
                c += 32;
        return Text(std::move(value));
    }
    [[nodiscard]] Text toUpper() const {
        auto value = points_;
        for (auto &c : value)
            if (c >= 'a' && c <= 'z')
                c -= 32;
        return Text(std::move(value));
    }
    template <class T> T numeric(bool *valid = nullptr) const {
        auto text = trimmed().toStdString();
        if (text.starts_with('+'))
            text.erase(0, 1);
        T value{};
        auto result = std::from_chars(text.data(), text.data() + text.size(), value);
        bool ok =
            result.ec == std::errc{} && result.ptr == text.data() + text.size() && !text.empty();
        if constexpr (std::is_floating_point_v<T>)
            ok = ok && std::isfinite(value);
        if (valid)
            *valid = ok;
        return ok ? value : T{};
    }
    int toInt(bool *valid = nullptr) const { return numeric<int>(valid); }
    double toDouble(bool *valid = nullptr) const { return numeric<double>(valid); }
    std::uint64_t toULongLong(bool *valid = nullptr) const { return numeric<std::uint64_t>(valid); }
    friend auto operator<=>(const Text &a, const Text &b) { return a.points_ <=> b.points_; }
    friend bool operator==(const Text &a, const Text &b) { return a.points_ == b.points_; }
    friend Text operator+(char32_t a, const Text &b) {
        Text value(a);
        value += b;
        return value;
    }
    friend Text operator+(Text a, const Text &b) {
        a += b;
        return a;
    }
    friend Text operator+(Text a, char32_t b) {
        a += b;
        return a;
    }
};
} // namespace ii::document::detail
