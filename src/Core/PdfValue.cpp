#include "Core/PdfValue.h"

#include "Core/Diagnostic.h"

#include <array>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>

namespace ii::document {
namespace {

bool isRegularNameByte(unsigned char value)
{
    if (value < 33 || value > 126) {
        return false;
    }
    switch (value) {
    case '#':
    case '%':
    case '(':
    case ')':
    case '/':
    case '<':
    case '>':
    case '[':
    case ']':
    case '{':
    case '}':
        return false;
    default:
        return true;
    }
}

std::string escapeName(std::string name)
{
    if (name.empty() || name.front() != '/') {
        name.insert(name.begin(), '/');
    }
    std::ostringstream result;
    result << '/';
    result << std::uppercase << std::hex << std::setfill('0');
    for (std::size_t index = 1; index < name.size(); ++index) {
        const auto byte = static_cast<unsigned char>(name[index]);
        if (isRegularNameByte(byte)) {
            result << static_cast<char>(byte);
        } else {
            result << '#' << std::setw(2) << static_cast<unsigned int>(byte);
        }
    }
    return result.str();
}

std::string escapeLiteralString(const std::string& bytes)
{
    std::string result;
    result.reserve(bytes.size() + 2);
    result.push_back('(');
    for (const char character : bytes) {
        const auto byte = static_cast<unsigned char>(character);
        switch (byte) {
        case '\\':
        case '(':
        case ')':
            result.push_back('\\');
            result.push_back(static_cast<char>(byte));
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        case '\t':
            result += "\\t";
            break;
        case '\b':
            result += "\\b";
            break;
        case '\f':
            result += "\\f";
            break;
        default:
            if (byte < 32 || byte > 126) {
                std::ostringstream octal;
                octal << '\\' << std::oct << std::setw(3) << std::setfill('0')
                      << static_cast<unsigned int>(byte);
                result += octal.str();
            } else {
                result.push_back(static_cast<char>(byte));
            }
        }
    }
    result.push_back(')');
    return result;
}

std::string formatReal(double value)
{
    if (!std::isfinite(value)) {
        throw DocumentError("PDF real values must be finite");
    }
    std::array<char, 128> buffer{};
    const auto conversion = std::to_chars(
        buffer.data(), buffer.data() + buffer.size(), value,
        std::chars_format::general);
    if (conversion.ec != std::errc{}) {
        throw DocumentError("Unable to serialize a PDF real value");
    }
    std::string result(buffer.data(), conversion.ptr);
    const auto exponentPosition = result.find_first_of("eE");
    if (exponentPosition != std::string::npos) {
        const int exponent = std::stoi(result.substr(exponentPosition + 1));
        std::string mantissa = result.substr(0, exponentPosition);
        std::string sign;
        if (!mantissa.empty() && (mantissa.front() == '-' || mantissa.front() == '+')) {
            sign.push_back(mantissa.front());
            mantissa.erase(mantissa.begin());
        }
        const auto decimal = mantissa.find('.');
        const auto originalDecimal = decimal == std::string::npos
            ? static_cast<std::ptrdiff_t>(mantissa.size())
            : static_cast<std::ptrdiff_t>(decimal);
        if (decimal != std::string::npos) {
            mantissa.erase(decimal, 1);
        }
        const auto targetDecimal = originalDecimal + exponent;
        if (targetDecimal <= 0) {
            result = sign + "0." + std::string(static_cast<std::size_t>(-targetDecimal), '0')
                + mantissa;
        } else if (targetDecimal >= static_cast<std::ptrdiff_t>(mantissa.size())) {
            result = sign + mantissa
                + std::string(
                    static_cast<std::size_t>(targetDecimal)
                        - mantissa.size(),
                    '0');
        } else {
            mantissa.insert(static_cast<std::size_t>(targetDecimal), 1, '.');
            result = sign + mantissa;
        }
    }
    if (const auto decimal = result.find('.'); decimal != std::string::npos) {
        while (result.size() > decimal + 1 && result.back() == '0') {
            result.pop_back();
        }
        if (result.back() == '.') {
            result.pop_back();
        }
    }
    return result == "-0" ? "0" : result;
}

} // namespace

PdfValue::PdfValue() = default;

PdfValue::PdfValue(PdfValueKind kind)
    : kind_(kind)
{
}

PdfValue PdfValue::null()
{
    return PdfValue{};
}

PdfValue PdfValue::boolean(bool value)
{
    PdfValue result(PdfValueKind::boolean);
    result.boolean_ = value;
    return result;
}

PdfValue PdfValue::integer(std::int64_t value)
{
    PdfValue result(PdfValueKind::integer);
    result.integer_ = value;
    return result;
}

PdfValue PdfValue::real(double value)
{
    return real(value, {});
}

PdfValue PdfValue::real(double value, std::string originalLexeme)
{
    PdfValue result(PdfValueKind::real);
    result.real_ = value;
    result.string_ = std::move(originalLexeme);
    return result;
}

PdfValue PdfValue::name(std::string canonicalName)
{
    PdfValue result(PdfValueKind::name);
    result.string_ = std::move(canonicalName);
    return result;
}

PdfValue PdfValue::string(std::string bytes)
{
    PdfValue result(PdfValueKind::string);
    result.string_ = std::move(bytes);
    return result;
}

PdfValue PdfValue::array(Array values)
{
    PdfValue result(PdfValueKind::array);
    result.array_ = std::move(values);
    return result;
}

PdfValue PdfValue::dictionary(Dictionary values)
{
    PdfValue result(PdfValueKind::dictionary);
    result.dictionary_ = std::move(values);
    return result;
}

PdfValue PdfValue::raw(std::string pdfSyntax)
{
    PdfValue result(PdfValueKind::raw);
    result.string_ = std::move(pdfSyntax);
    return result;
}

PdfValueKind PdfValue::kind() const noexcept
{
    return kind_;
}

bool PdfValue::booleanValue() const
{
    if (kind_ != PdfValueKind::boolean) {
        throw DocumentError("PDF value is not a boolean");
    }
    return boolean_;
}

std::int64_t PdfValue::integerValue() const
{
    if (kind_ != PdfValueKind::integer) {
        throw DocumentError("PDF value is not an integer");
    }
    return integer_;
}

double PdfValue::realValue() const
{
    if (kind_ != PdfValueKind::real && kind_ != PdfValueKind::integer) {
        throw DocumentError("PDF value is not numeric");
    }
    return kind_ == PdfValueKind::integer ? static_cast<double>(integer_) : real_;
}

const std::string& PdfValue::stringValue() const
{
    if (kind_ != PdfValueKind::name && kind_ != PdfValueKind::string
        && kind_ != PdfValueKind::raw) {
        throw DocumentError("PDF value does not contain string data");
    }
    return string_;
}

std::string& PdfValue::stringValue()
{
    return const_cast<std::string&>(std::as_const(*this).stringValue());
}

const PdfValue::Array& PdfValue::arrayItems() const
{
    if (kind_ != PdfValueKind::array) {
        throw DocumentError("PDF value is not an array");
    }
    return array_;
}

PdfValue::Array& PdfValue::arrayItems()
{
    return const_cast<Array&>(std::as_const(*this).arrayItems());
}

const PdfValue::Dictionary& PdfValue::dictionaryItems() const
{
    if (kind_ != PdfValueKind::dictionary) {
        throw DocumentError("PDF value is not a dictionary");
    }
    return dictionary_;
}

PdfValue::Dictionary& PdfValue::dictionaryItems()
{
    return const_cast<Dictionary&>(std::as_const(*this).dictionaryItems());
}

std::string PdfValue::toPdfSyntax() const
{
    switch (kind_) {
    case PdfValueKind::null:
        return "null";
    case PdfValueKind::boolean:
        return boolean_ ? "true" : "false";
    case PdfValueKind::integer:
        return std::to_string(integer_);
    case PdfValueKind::real:
        return string_.empty() ? formatReal(real_) : string_;
    case PdfValueKind::name:
        return escapeName(string_);
    case PdfValueKind::string:
        return escapeLiteralString(string_);
    case PdfValueKind::array: {
        std::string result{"["};
        for (std::size_t index = 0; index < array_.size(); ++index) {
            if (index != 0) {
                result.push_back(' ');
            }
            result += array_[index].toPdfSyntax();
        }
        result.push_back(']');
        return result;
    }
    case PdfValueKind::dictionary: {
        std::string result{"<<"};
        for (const auto& [key, value] : dictionary_) {
            result.push_back(' ');
            result += escapeName(key);
            result.push_back(' ');
            result += value.toPdfSyntax();
        }
        result += " >>";
        return result;
    }
    case PdfValueKind::raw:
        return string_;
    }
    throw DocumentError("Unknown PDF value kind");
}

bool PdfInstruction::isInlineImageData() const noexcept
{
    return !inlineImageData.empty() && operatorName.empty();
}

std::string PdfInstruction::toPdfSyntax() const
{
    if (isInlineImageData()) {
        return inlineImageData;
    }
    std::string result;
    for (const auto& operand : operands) {
        if (!result.empty()) {
            result.push_back(' ');
        }
        result += operand.toPdfSyntax();
    }
    if (!operatorName.empty()) {
        if (!result.empty()) {
            result.push_back(' ');
        }
        result += operatorName;
    }
    result.push_back('\n');
    return result;
}

} // namespace ii::document
