#include "Word/WordDocumentReader.h"

#include "Word/Private/DocxPackage.h"
#include "Word/Private/LegacyDocConverter.h"
#include "Word/Private/OdfTextCodec.h"

#include <QTemporaryDir>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iterator>
#include <string>
#include <utility>

namespace ii::document {
namespace {

std::string lowercaseExtension(const std::filesystem::path& path)
{
    auto extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return extension;
}

} // namespace

WordReadResult WordDocumentReader::read(
    const std::filesystem::path& source,
    const WordReadOptions& options) const
{
    const auto extension = lowercaseExtension(source);
    if (extension == ".docx") {
        return detail::readDocxPackage(source, options);
    }
    if (extension == ".odt") {
        return detail::readOdtPackage(source, options);
    }
    if (extension == ".fodt") {
        return detail::readFodtDocument(source, options);
    }
    if (extension != ".doc") {
        WordReadResult result;
        result.diagnostics.push_back({
            DiagnosticSeverity::error,
            "word.unsupported_extension",
            "WordDocumentReader supports .doc, .docx, .odt, and .fodt files.",
            source.string(),
        });
        return result;
    }

    QTemporaryDir bridgeDirectory;
    WordReadResult result;
    if (!bridgeDirectory.isValid()) {
        result.diagnostics.push_back({
            DiagnosticSeverity::error,
            "doc.temporary_directory_failed",
            "Unable to create a bridge directory for legacy Word input.",
            source.string(),
        });
        return result;
    }
    const auto converted = std::filesystem::path(bridgeDirectory.path().toStdString())
        / (source.stem().string() + ".docx");
    result.diagnostics = detail::convertWordFile(
        source, converted, options.libreOfficeExecutable, options.conversionTimeout,
        "docx:Office Open XML Text");
    if (result.hasErrors()) {
        return result;
    }

    auto parsed = detail::readDocxPackage(converted, options);
    parsed.diagnostics.insert(parsed.diagnostics.begin(),
                              std::make_move_iterator(result.diagnostics.begin()),
                              std::make_move_iterator(result.diagnostics.end()));
    return parsed;
}

} // namespace ii::document
