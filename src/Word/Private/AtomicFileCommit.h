#pragma once

#include <filesystem>
#include <string>

namespace ii::document::detail {

struct AtomicFileCommitResult {
    bool succeeded{false};
    std::string diagnosticSuffix;
    std::string message;
};

[[nodiscard]] AtomicFileCommitResult atomicReplacePreservingPermissions(
    const std::filesystem::path& temporary,
    const std::filesystem::path& destination);

} // namespace ii::document::detail
