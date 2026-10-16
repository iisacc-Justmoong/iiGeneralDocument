#include "Word/Private/AtomicFileCommit.h"
#include <iiFileProviderNative.h>

namespace ii::document::detail {
AtomicFileCommitResult atomicReplacePreservingPermissions(
    const std::filesystem::path &temporary, const std::filesystem::path &destination)
{
    const auto result = iiFileProvider::native::File::publish(temporary, destination);
    return {result.succeeded, result.diagnosticSuffix, result.message};
}
} // namespace ii::document::detail
