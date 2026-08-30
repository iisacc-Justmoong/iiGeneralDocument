#pragma once

#include "iiGeneralDocument/Export.h"

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ii::document {

enum class DiagnosticSeverity {
    information,
    warning,
    error,
};

struct IIGENERALDOCUMENT_EXPORT Diagnostic {
    DiagnosticSeverity severity{DiagnosticSeverity::information};
    std::string code;
    std::string message;
    std::string context;
};

class IIGENERALDOCUMENT_EXPORT DocumentError : public std::runtime_error {
public:
    explicit DocumentError(std::string message);
};

IIGENERALDOCUMENT_EXPORT bool hasErrors(const std::vector<Diagnostic>& diagnostics) noexcept;

} // namespace ii::document
