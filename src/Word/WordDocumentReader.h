#pragma once

#include "Core/Diagnostic.h"
#include "Word/WordDocument.h"
#include "iiGeneralDocument/Export.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace ii::document {

struct WordReadOptions {
    std::filesystem::path libreOfficeExecutable;
    std::chrono::milliseconds conversionTimeout{60000};
    std::uint64_t maximumXmlPartBytes{64ULL * 1024ULL * 1024ULL};
};

struct IIGENERALDOCUMENT_EXPORT WordReadResult {
    WordDocument document;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool hasErrors() const noexcept
    {
        return ii::document::hasErrors(diagnostics);
    }
};

class IIGENERALDOCUMENT_EXPORT WordDocumentReader final {
public:
    [[nodiscard]] WordReadResult read(
        const std::filesystem::path& source,
        const WordReadOptions& options = {}) const;
};

} // namespace ii::document
