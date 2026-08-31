#pragma once

#include "Core/Diagnostic.h"

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace ii::document::detail {

[[nodiscard]] std::vector<Diagnostic> convertWordFile(
    const std::filesystem::path& source,
    const std::filesystem::path& destination,
    const std::filesystem::path& requestedExecutable,
    std::chrono::milliseconds timeout,
    std::string outputFilter);

} // namespace ii::document::detail
