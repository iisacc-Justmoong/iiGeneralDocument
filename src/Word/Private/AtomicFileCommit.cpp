#include "Word/Private/AtomicFileCommit.h"

#include <cerrno>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace ii::document::detail {

#ifndef _WIN32
namespace {

std::error_code posixError(int errorNumber)
{
    return {errorNumber, std::generic_category()};
}

AtomicFileCommitResult applyOrdinaryNewFilePermissions(
    const std::filesystem::path& temporary)
{
    auto probe = temporary;
    probe += ".permission-probe";

    int flags = O_WRONLY | O_CREAT | O_EXCL;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
    const int descriptor = ::open(probe.c_str(), flags, 0666);
    if (descriptor < 0) {
        const auto error = posixError(errno);
        return {
            false,
            "destination_permission_probe_failed",
            "The new-destination permission probe could not be created: "
                + error.message()};
    }

    struct stat status {};
    const int statusResult = ::fstat(descriptor, &status);
    const int statusError = statusResult == 0 ? 0 : errno;

    const int unlinkResult = ::unlink(probe.c_str());
    const int unlinkError = unlinkResult == 0 ? 0 : errno;
    const int closeResult = ::close(descriptor);
    const int closeError = closeResult == 0 ? 0 : errno;

    if (statusError != 0) {
        return {
            false,
            "destination_permission_probe_status_failed",
            "The new-destination permission probe mode could not be read: "
                + posixError(statusError).message()};
    }
    if (unlinkError != 0) {
        return {
            false,
            "destination_permission_probe_cleanup_failed",
            "The new-destination permission probe could not be removed: "
                + posixError(unlinkError).message()};
    }
    if (closeError != 0) {
        return {
            false,
            "destination_permission_probe_close_failed",
            "The new-destination permission probe could not be closed: "
                + posixError(closeError).message()};
    }

    const mode_t permissions = status.st_mode
        & static_cast<mode_t>(S_IRWXU | S_IRWXG | S_IRWXO);
    if (::chmod(temporary.c_str(), permissions) != 0) {
        const auto error = posixError(errno);
        return {
            false,
            "temporary_permissions_failed",
            "The ordinary new-file permissions could not be applied to the replacement: "
                + error.message()};
    }
    return {true, {}, {}};
}

} // namespace
#endif

AtomicFileCommitResult atomicReplacePreservingPermissions(
    const std::filesystem::path& temporary,
    const std::filesystem::path& destination)
{
    std::error_code error;
    const bool destinationExists = std::filesystem::exists(destination, error);
    if (error) {
        return {
            false,
            "destination_status_failed",
            "The existing destination could not be inspected: " + error.message()};
    }
    if (destinationExists) {
        const auto destinationPermissions =
            std::filesystem::status(destination, error).permissions();
        if (error) {
            return {
                false,
                "destination_permissions_failed",
                "The existing destination permissions could not be read: "
                    + error.message()};
        }
        std::filesystem::permissions(
            temporary,
            destinationPermissions,
            std::filesystem::perm_options::replace,
            error);
        if (error) {
            return {
                false,
                "temporary_permissions_failed",
                "The replacement permissions could not be preserved: "
                    + error.message()};
        }
    }
#ifndef _WIN32
    else {
        const auto permissions = applyOrdinaryNewFilePermissions(temporary);
        if (!permissions.succeeded) {
            return permissions;
        }
    }
#endif

#ifdef _WIN32
    if (!MoveFileExW(
            temporary.c_str(), destination.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error = std::error_code(
            static_cast<int>(GetLastError()), std::system_category());
    }
#else
    std::filesystem::rename(temporary, destination, error);
#endif
    if (error) {
        return {
            false,
            "atomic_commit_failed",
            "The validated temporary file could not replace its destination atomically: "
                + error.message()};
    }
    return {true, {}, {}};
}

} // namespace ii::document::detail
