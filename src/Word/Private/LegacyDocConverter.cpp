#include "Word/Private/LegacyDocConverter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUrl>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace ii::document::detail {
namespace {

QString toQString(const std::filesystem::path& path)
{
#ifdef _WIN32
    return QString::fromStdWString(path.wstring());
#else
    return QString::fromUtf8(path.string());
#endif
}

std::string toUtf8(const QString& value)
{
    const auto bytes = value.toUtf8();
    return {bytes.constData(), static_cast<std::size_t>(bytes.size())};
}

std::filesystem::path resolveExecutable(const std::filesystem::path& requested)
{
    if (!requested.empty()) {
        return requested;
    }

    for (const auto* name : {"soffice", "libreoffice"}) {
        const auto found = QStandardPaths::findExecutable(QString::fromLatin1(name));
        if (!found.isEmpty()) {
            return std::filesystem::path(toUtf8(found));
        }
    }

    const std::array<std::filesystem::path, 2> commonPaths{
        std::filesystem::path{"/Applications/LibreOffice.app/Contents/MacOS/soffice"},
        std::filesystem::path{"C:/Program Files/LibreOffice/program/soffice.exe"},
    };
    for (const auto& candidate : commonPaths) {
        if (std::filesystem::is_regular_file(candidate)) {
            return candidate;
        }
    }
    return {};
}

Diagnostic error(std::string code, std::string message, const std::filesystem::path& context)
{
    return {DiagnosticSeverity::error, std::move(code), std::move(message),
            context.string()};
}

bool copyAtomically(const QString& source, const QString& destination, QString& message)
{
    QFile input(source);
    if (!input.open(QIODevice::ReadOnly)) {
        message = input.errorString();
        return false;
    }

    QSaveFile output(destination);
    if (!output.open(QIODevice::WriteOnly)) {
        message = output.errorString();
        return false;
    }

    std::array<char, 64 * 1024> buffer{};
    while (true) {
        const auto read = input.read(buffer.data(), static_cast<qint64>(buffer.size()));
        if (read < 0) {
            message = input.errorString();
            output.cancelWriting();
            return false;
        }
        if (read == 0) {
            break;
        }
        if (output.write(buffer.data(), read) != read) {
            message = output.errorString();
            output.cancelWriting();
            return false;
        }
    }

    if (!output.commit()) {
        message = output.errorString();
        return false;
    }
    return true;
}

} // namespace

std::vector<Diagnostic> convertWordFile(
    const std::filesystem::path& source,
    const std::filesystem::path& destination,
    const std::filesystem::path& requestedExecutable,
    std::chrono::milliseconds timeout,
    std::string outputFilter)
{
    std::vector<Diagnostic> diagnostics;
    const auto executable = resolveExecutable(requestedExecutable);
    if (executable.empty() || !std::filesystem::is_regular_file(executable)) {
        diagnostics.push_back(error(
            "doc.converter_missing",
            "LibreOffice soffice is required for the legacy .doc format.",
            requestedExecutable.empty() ? source : requestedExecutable));
        return diagnostics;
    }
    if (!std::filesystem::is_regular_file(source)) {
        diagnostics.push_back(error(
            "doc.source_missing", "The Word conversion source does not exist.", source));
        return diagnostics;
    }
    if (timeout <= std::chrono::milliseconds::zero()) {
        diagnostics.push_back(error(
            "doc.invalid_timeout", "The Word conversion timeout must be positive.", source));
        return diagnostics;
    }

    QTemporaryDir workDirectory;
    QTemporaryDir profileDirectory;
    if (!workDirectory.isValid() || !profileDirectory.isValid()) {
        diagnostics.push_back(error(
            "doc.temporary_directory_failed",
            "Unable to create an isolated directory for Word conversion.", destination));
        return diagnostics;
    }

    QProcess process;
    process.setProgram(toQString(executable));
    process.setWorkingDirectory(workDirectory.path());
    process.setProcessChannelMode(QProcess::SeparateChannels);
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("HOME"), profileDirectory.path());
    process.setProcessEnvironment(environment);

    const auto profileUrl = QUrl::fromLocalFile(profileDirectory.path())
                                .toString(QUrl::FullyEncoded);
    const auto filter = QString::fromUtf8(outputFilter);
    const auto outputExtension = filter.section(QLatin1Char(':'), 0, 0);
    process.setArguments({
        QStringLiteral("--headless"),
        QStringLiteral("--nologo"),
        QStringLiteral("--nodefault"),
        QStringLiteral("--nolockcheck"),
        QStringLiteral("--norestore"),
        QStringLiteral("-env:UserInstallation=") + profileUrl,
        QStringLiteral("--convert-to"),
        filter,
        QStringLiteral("--outdir"),
        workDirectory.path(),
        toQString(std::filesystem::absolute(source)),
    });

    process.start();
    const auto timeoutCount = std::min<std::chrono::milliseconds::rep>(
        timeout.count(), std::numeric_limits<int>::max());
    const auto timeoutMilliseconds = static_cast<int>(timeoutCount);
    if (!process.waitForStarted(timeoutMilliseconds)) {
        diagnostics.push_back(error(
            "doc.conversion_start_failed",
            "LibreOffice could not start: " + toUtf8(process.errorString()), executable));
        return diagnostics;
    }
    if (!process.waitForFinished(timeoutMilliseconds)) {
        process.kill();
        process.waitForFinished(5000);
        diagnostics.push_back(error(
            "doc.conversion_timeout", "LibreOffice Word conversion timed out.", source));
        return diagnostics;
    }

    const auto standardError = toUtf8(QString::fromUtf8(process.readAllStandardError()).trimmed());
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        diagnostics.push_back(error(
            "doc.conversion_failed",
            "LibreOffice failed to convert the Word file"
                + (standardError.empty() ? std::string{"."} : ": " + standardError),
            source));
        return diagnostics;
    }

    const auto convertedName = QFileInfo(toQString(source)).completeBaseName()
        + QLatin1Char('.') + outputExtension;
    const auto convertedPath = QDir(workDirectory.path()).filePath(convertedName);
    if (!QFileInfo::exists(convertedPath) || QFileInfo(convertedPath).size() <= 0) {
        diagnostics.push_back(error(
            "doc.conversion_output_missing",
            "LibreOffice reported success but did not produce the expected Word file.", source));
        return diagnostics;
    }

    const auto parent = toQString(destination.parent_path());
    if (!parent.isEmpty() && !QDir().mkpath(parent)) {
        diagnostics.push_back(error(
            "doc.destination_directory_failed",
            "Unable to create the Word destination directory.", destination.parent_path()));
        return diagnostics;
    }

    QString copyError;
    if (!copyAtomically(convertedPath, toQString(destination), copyError)) {
        diagnostics.push_back(error(
            "doc.destination_write_failed",
            "Unable to commit the converted Word file: " + toUtf8(copyError), destination));
        return diagnostics;
    }

    diagnostics.push_back({
        DiagnosticSeverity::information,
        "doc.converted_with_libreoffice",
        "The legacy Word document was converted through an isolated LibreOffice process.",
        destination.string(),
    });
    return diagnostics;
}

} // namespace ii::document::detail
