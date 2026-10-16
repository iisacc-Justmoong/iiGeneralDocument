#pragma once

#include "IO/DocumentWriter.h"
#include "iiGeneralDocument/Export.h"

namespace ii::document {

struct IIGENERALDOCUMENT_EXPORT PdfBytesResult : WriteResult {
    pdf::Bytes bytes;
};

class IIGENERALDOCUMENT_EXPORT PdfDocumentWriter final : public DocumentWriter {
public:
    [[nodiscard]] WriteResult write(
        const Document& document,
        const std::filesystem::path& destination,
        const WriteOptions& options = {}) const override;
    [[nodiscard]] PdfBytesResult writeBytes(const Document& document,
        const WriteOptions& options = {}) const;
};

} // namespace ii::document
