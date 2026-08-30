#pragma once

#include "IO/DocumentReader.h"
#include "iiGeneralDocument/Export.h"

namespace ii::document {

class IIGENERALDOCUMENT_EXPORT PdfDocumentReader final : public DocumentReader {
public:
    [[nodiscard]] ReadResult read(
        const std::filesystem::path& source,
        const ReadOptions& options = {}) const override;
};

} // namespace ii::document
