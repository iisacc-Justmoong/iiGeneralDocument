#pragma once

#include "Word/WordDocumentReader.h"
#include "Word/WordDocumentWriter.h"

namespace ii::document::detail {

[[nodiscard]] WordReadResult readDocxPackage(
    const std::filesystem::path& source,
    const WordReadOptions& options);

[[nodiscard]] WordWriteResult writeDocxPackage(
    const WordDocument& document,
    const std::filesystem::path& destination,
    const WordWriteOptions& options);

} // namespace ii::document::detail
