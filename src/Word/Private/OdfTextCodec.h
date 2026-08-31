#pragma once

#include "Word/WordDocumentReader.h"
#include "Word/WordDocumentWriter.h"

namespace ii::document::detail {

[[nodiscard]] WordReadResult readOdtPackage(
    const std::filesystem::path& source,
    const WordReadOptions& options);

[[nodiscard]] WordWriteResult writeOdtPackage(
    const WordDocument& document,
    const std::filesystem::path& destination,
    const WordWriteOptions& options);

[[nodiscard]] WordReadResult readFodtDocument(
    const std::filesystem::path& source,
    const WordReadOptions& options);

[[nodiscard]] WordWriteResult writeFodtDocument(
    const WordDocument& document,
    const std::filesystem::path& destination,
    const WordWriteOptions& options);

} // namespace ii::document::detail
