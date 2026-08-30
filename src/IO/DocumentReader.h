#pragma once

#include "Core/Diagnostic.h"
#include "Model/Document.h"
#include "iiGeneralDocument/Export.h"

#include <filesystem>
#include <string>
#include <vector>

namespace ii::document {

struct ReadOptions {
    std::string password;
    bool attemptRecovery{true};
};

struct IIGENERALDOCUMENT_EXPORT ReadResult {
    Document document;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool hasErrors() const noexcept
    {
        return ii::document::hasErrors(diagnostics);
    }
};

class IIGENERALDOCUMENT_EXPORT DocumentReader {
public:
    virtual ~DocumentReader() = default;
    [[nodiscard]] virtual ReadResult read(
        const std::filesystem::path& source,
        const ReadOptions& options = {}) const = 0;
};

} // namespace ii::document
