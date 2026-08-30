#pragma once

#include "iiGeneralDocument/Export.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace ii::document {

enum class PdfValueKind {
    null,
    boolean,
    integer,
    real,
    name,
    string,
    array,
    dictionary,
    raw,
};

class IIGENERALDOCUMENT_EXPORT PdfValue {
public:
    using Array = std::vector<PdfValue>;
    using Dictionary = std::vector<std::pair<std::string, PdfValue>>;

    PdfValue();

    static PdfValue null();
    static PdfValue boolean(bool value);
    static PdfValue integer(std::int64_t value);
    static PdfValue real(double value);
    static PdfValue real(double value, std::string originalLexeme);
    static PdfValue name(std::string canonicalName);
    static PdfValue string(std::string bytes);
    static PdfValue array(Array values);
    static PdfValue dictionary(Dictionary values);
    static PdfValue raw(std::string pdfSyntax);

    [[nodiscard]] PdfValueKind kind() const noexcept;
    [[nodiscard]] bool booleanValue() const;
    [[nodiscard]] std::int64_t integerValue() const;
    [[nodiscard]] double realValue() const;
    [[nodiscard]] const std::string& stringValue() const;
    [[nodiscard]] std::string& stringValue();
    [[nodiscard]] const Array& arrayItems() const;
    [[nodiscard]] Array& arrayItems();
    [[nodiscard]] const Dictionary& dictionaryItems() const;
    [[nodiscard]] Dictionary& dictionaryItems();

    [[nodiscard]] std::string toPdfSyntax() const;

private:
    explicit PdfValue(PdfValueKind kind);

    PdfValueKind kind_{PdfValueKind::null};
    bool boolean_{false};
    std::int64_t integer_{0};
    double real_{0.0};
    std::string string_;
    Array array_;
    Dictionary dictionary_;
};

struct IIGENERALDOCUMENT_EXPORT PdfInstruction {
    std::vector<PdfValue> operands;
    std::string operatorName;
    std::string inlineImageData;

    [[nodiscard]] bool isInlineImageData() const noexcept;
    [[nodiscard]] std::string toPdfSyntax() const;
};

} // namespace ii::document
