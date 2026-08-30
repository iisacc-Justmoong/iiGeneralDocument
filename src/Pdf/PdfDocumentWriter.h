#pragma once

#include "IO/DocumentWriter.h"
#include "iiGeneralDocument/Export.h"

namespace ii::document {

class IIGENERALDOCUMENT_EXPORT PdfDocumentWriter final : public DocumentWriter {
public:
    [[nodiscard]] WriteResult write(
        const Document& document,
        const std::filesystem::path& destination,
        const WriteOptions& options = {}) const override;
};

} // namespace ii::document
