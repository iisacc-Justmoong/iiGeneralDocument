#pragma once

#include "Core/Diagnostic.h"
#include "Model/Document.h"
#include "iiGeneralDocument/Export.h"

#include <filesystem>
#include <vector>

namespace ii::document {

struct WriteOptions {
    bool linearize{false};
    bool allowInvalidatingDigitalSignatures{false};
    bool allowRemovingEncryption{false};
};

struct IIGENERALDOCUMENT_EXPORT WriteResult {
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool hasErrors() const noexcept
    {
        return ii::document::hasErrors(diagnostics);
    }
};

class IIGENERALDOCUMENT_EXPORT DocumentWriter {
public:
    virtual ~DocumentWriter() = default;
    [[nodiscard]] virtual WriteResult write(
        const Document& document,
        const std::filesystem::path& destination,
        const WriteOptions& options = {}) const = 0;
};

} // namespace ii::document
