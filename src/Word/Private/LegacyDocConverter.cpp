#include "Word/Private/LegacyDocConverter.h"
#include "Word/Private/ConversionProcess.h"
#include <array>
#include <cstdlib>
#include <iiFileProviderNative.h>
#include <system_error>
namespace ii::document::detail {
namespace {
std::filesystem::path resolveExecutable(const std::filesystem::path &requested) {
    if (!requested.empty())
        return std::filesystem::absolute(requested);
    if (auto *search = std::getenv("PATH")) {
        std::string_view paths(search);
#ifdef _WIN32
        constexpr char delimiter = ';';
        constexpr std::array names{"soffice.com", "soffice.exe", "libreoffice.exe"};
#else
        constexpr char delimiter = ':';
        constexpr std::array names{"soffice", "libreoffice"};
#endif
        while (!paths.empty()) {
            auto end = paths.find(delimiter);
            auto directory = std::filesystem::path(std::string(paths.substr(0, end)));
            for (auto name : names) {
                auto path = directory / name;
                if (std::filesystem::is_regular_file(path))
                    return std::filesystem::absolute(path);
            }
            if (end == paths.npos)
                break;
            paths.remove_prefix(end + 1);
        }
    }
    for (auto path : {"/Applications/LibreOffice.app/Contents/MacOS/soffice",
                      "C:/Program Files/LibreOffice/program/soffice.exe"})
        if (std::filesystem::is_regular_file(path))
            return path;
    return {};
}
Diagnostic error(std::string code, std::string message, const std::filesystem::path &context) {
    return {DiagnosticSeverity::error, std::move(code), std::move(message), context.string()};
}
} // namespace
std::vector<Diagnostic> convertWordFile(const std::filesystem::path &source,
                                        const std::filesystem::path &destination,
                                        const std::filesystem::path &requestedExecutable,
                                        std::chrono::milliseconds timeout,
                                        std::string outputFilter) {
    auto executable = resolveExecutable(requestedExecutable);
    if (executable.empty() || !std::filesystem::is_regular_file(executable))
        return {error("doc.converter_missing",
                      "LibreOffice soffice is required for the legacy .doc format.",
                      requestedExecutable.empty() ? source : requestedExecutable)};
    if (!std::filesystem::is_regular_file(source))
        return {error("doc.source_missing", "The Word conversion source does not exist.", source)};
    if (timeout <= std::chrono::milliseconds::zero())
        return {
            error("doc.invalid_timeout", "The Word conversion timeout must be positive.", source)};
    try {
        iiFileProvider::native::TemporaryDirectory work, profile;
        auto run = runConversionProcess(
            executable,
            {"--headless", "--nologo", "--nodefault", "--nolockcheck", "--norestore",
             "-env:UserInstallation=" + localFileUri(profile.path()), "--convert-to", outputFilter,
             "--outdir", pathUtf8(work.path()), pathUtf8(std::filesystem::absolute(source))},
            work.path(), timeout);
        if (!run.started)
            return {error("doc.conversion_start_failed",
                          "LibreOffice could not start: " + run.error, executable)};
        if (run.timedOut)
            return {
                error("doc.conversion_timeout", "LibreOffice Word conversion timed out.", source)};
        if (run.exitCode != 0)
            return {error("doc.conversion_failed",
                          "LibreOffice failed to convert the Word file: " + run.error, source)};
        auto extension = outputFilter.substr(0, outputFilter.find(':'));
        auto converted = work.path() / (source.stem().string() + "." + extension);
        if (!std::filesystem::is_regular_file(converted) ||
            std::filesystem::file_size(converted) == 0)
            return {error("doc.conversion_output_missing",
                          "LibreOffice reported success but did not produce the "
                          "expected Word file.",
                          source)};
        // Native provider performs the atomic publication and owns all file access.
        iiFileProvider::native::File::write(destination,
                                            iiFileProvider::native::File::read(converted));
        return {{DiagnosticSeverity::information, "doc.converted_with_libreoffice",
                 "The legacy Word document was converted through an isolated "
                 "LibreOffice process.",
                 destination.string()}};
    } catch (const std::exception &e) {
        return {error("doc.destination_write_failed", e.what(), destination)};
    }
}
} // namespace ii::document::detail
