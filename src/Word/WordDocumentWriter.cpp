#include "Word/WordDocumentWriter.h"

#include "Word/Private/DocxPackage.h"
#include "Word/Private/LegacyDocConverter.h"

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

WordWriteResult WordDocumentWriter::write(
    const WordDocument& document,
    const std::filesystem::path& destination,
    const WordWriteOptions& options) const
{
    const auto extension = lowercaseExtension(destination);
    if (extension == ".docx") {
        return detail::writeDocxPackage(document, destination, options);
    }
    if (extension != ".doc") {
        WordWriteResult result;
        result.diagnostics.push_back({
            DiagnosticSeverity::error,
            "word.unsupported_extension",
            "WordDocumentWriter supports only .doc and .docx files.",
            destination.string(),
        });
        return result;
    }

    QTemporaryDir bridgeDirectory;
    WordWriteResult result;
    if (!bridgeDirectory.isValid()) {
        result.diagnostics.push_back({
            DiagnosticSeverity::error,
            "doc.temporary_directory_failed",
            "Unable to create a bridge directory for legacy Word output.",
            destination.string(),
        });
        return result;
    }
    const auto bridge = std::filesystem::path(bridgeDirectory.path().toStdString())
        / (destination.stem().string() + ".docx");
    result = detail::writeDocxPackage(document, bridge, options);
    if (result.hasErrors()) {
        return result;
    }

    auto conversionDiagnostics = detail::convertWordFile(
        bridge, destination, options.libreOfficeExecutable, options.conversionTimeout,
        "doc:MS Word 97");
    result.diagnostics.insert(result.diagnostics.end(),
                              std::make_move_iterator(conversionDiagnostics.begin()),
                              std::make_move_iterator(conversionDiagnostics.end()));
    if (result.hasErrors()) {
        return result;
    }

    QTemporaryDir validationDirectory;
    if (!validationDirectory.isValid()) {
        result.diagnostics.push_back({
            DiagnosticSeverity::error,
            "doc.post_write_validation_failed",
            "Unable to create a directory for legacy Word post-write validation.",
            destination.string(),
        });
        return result;
    }
    const auto reopenedDocx = std::filesystem::path(validationDirectory.path().toStdString())
        / (destination.stem().string() + ".docx");
    auto validationConversion = detail::convertWordFile(
        destination, reopenedDocx, options.libreOfficeExecutable, options.conversionTimeout,
        "docx:Office Open XML Text");
    if (ii::document::hasErrors(validationConversion)) {
        result.diagnostics.push_back({
            DiagnosticSeverity::error,
            "doc.post_write_validation_failed",
            "The committed legacy Word file could not be converted back for validation.",
            destination.string(),
        });
        result.diagnostics.insert(
            result.diagnostics.end(),
            std::make_move_iterator(validationConversion.begin()),
            std::make_move_iterator(validationConversion.end()));
        return result;
    }

    WordReadOptions validationOptions;
    auto validation = detail::readDocxPackage(reopenedDocx, validationOptions);
    if (validation.hasErrors()) {
        result.diagnostics.push_back({
            DiagnosticSeverity::error,
            "doc.post_write_validation_failed",
            "The committed legacy Word file could not be reopened as a Word document.",
            destination.string(),
        });
        result.diagnostics.insert(
            result.diagnostics.end(),
            std::make_move_iterator(validation.diagnostics.begin()),
            std::make_move_iterator(validation.diagnostics.end()));
    }
    return result;
}

} // namespace ii::document
