#include "Core/Diagnostic.h"

#include <algorithm>

namespace ii::document {

DocumentError::DocumentError(std::string message)
    : std::runtime_error(std::move(message))
{
}

bool hasErrors(const std::vector<Diagnostic>& diagnostics) noexcept
{
    return std::ranges::any_of(diagnostics, [](const Diagnostic& diagnostic) {
        return diagnostic.severity == DiagnosticSeverity::error;
    });
}

} // namespace ii::document
