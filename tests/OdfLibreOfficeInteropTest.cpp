#include "TestSupport.h"
#include "Word/WordDocumentReader.h"
#include "Word/WordDocumentWriter.h"

#include <QProcess>
#include <QString>
#include <QTemporaryDir>
#include <QUrl>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#if !defined(TEST_OUTPUT_DIR) && defined(IIGENERALDOCUMENT_TEST_OUTPUT_DIR)
#define TEST_OUTPUT_DIR IIGENERALDOCUMENT_TEST_OUTPUT_DIR
#endif

#ifndef TEST_OUTPUT_DIR
#error "TEST_OUTPUT_DIR must identify the interoperability-test output directory."
#endif

#ifndef IIGENERALDOCUMENT_TEST_SOFFICE
#error "IIGENERALDOCUMENT_TEST_SOFFICE must identify the LibreOffice executable."
#endif

using namespace ii::document;

namespace {

constexpr int conversionTimeoutMilliseconds = 90000;
constexpr std::string_view unicodeBody =
    "ODF 상호운용 본문 — English, 日本語, emoji 😀";

QString toQString(const std::filesystem::path& path)
{
#ifdef _WIN32
    return QString::fromStdWString(path.wstring());
#else
    return QString::fromUtf8(path.string());
#endif
}

void printDiagnostics(
    std::string_view operation,
    const std::vector<Diagnostic>& diagnostics)
{
    for (const auto& item : diagnostics) {
        std::cerr << operation << ": " << item.code << ": " << item.message
                  << " [" << item.context << "]\n";
    }
}

WordDocument sampleDocument()
{
    WordDocument document;
    document.metadata()["Title"] = "LibreOffice ODF interoperability";
    document.metadata()["Author"] = "iiGeneralDocument";

    WordParagraph heading;
    heading.properties.styleId = "Heading2";
    heading.properties.alignment = WordParagraphAlignment::center;
    heading.runs.push_back({"Interoperability heading", {
        .bold = true, .fontSizePoints = 16.0, .color = "1F4E78"}});
    document.appendParagraph(std::move(heading));

    WordParagraph paragraph;
    paragraph.runs.push_back({std::string(unicodeBody), {.italic = true}});
    document.appendParagraph(std::move(paragraph));

    WordParagraph numbered;
    numbered.properties.numberingId = 3;
    numbered.properties.numberingLevel = 1;
    numbered.runs.push_back({"Interoperability list item"});
    document.appendParagraph(std::move(numbered));

    WordTable table;
    WordTableRow row;
    for (const auto* value : {"format", "ODF"}) {
        WordTableCell cell;
        WordParagraph cellParagraph;
        cellParagraph.runs.push_back({value});
        cell.paragraphs.push_back(std::move(cellParagraph));
        row.cells.push_back(std::move(cell));
    }
    table.rows.push_back(std::move(row));
    document.appendTable(std::move(table));
    return document;
}

struct ConversionResult {
    std::filesystem::path output;
    bool succeeded{false};
};

ConversionResult convertWithLibreOffice(
    const std::filesystem::path& source,
    const std::filesystem::path& outputDirectory,
    std::string_view filter,
    std::string_view outputExtension)
{
    ConversionResult result;
    result.output = outputDirectory
        / (source.stem().string() + std::string(outputExtension));

    QTemporaryDir profileDirectory;
    if (!profileDirectory.isValid()) {
        std::cerr << "LibreOffice conversion could not create an isolated profile for "
                  << source << "\n";
        return result;
    }

    QProcess process;
    process.setProgram(QString::fromUtf8(IIGENERALDOCUMENT_TEST_SOFFICE));
    process.setWorkingDirectory(toQString(outputDirectory));
    process.setProcessChannelMode(QProcess::SeparateChannels);

    const auto profileUrl = QUrl::fromLocalFile(profileDirectory.path())
                                .toString(QUrl::FullyEncoded);
    process.setArguments({
        QStringLiteral("--headless"),
        QStringLiteral("--nologo"),
        QStringLiteral("--nodefault"),
        QStringLiteral("--nolockcheck"),
        QStringLiteral("--norestore"),
        QStringLiteral("-env:UserInstallation=") + profileUrl,
        QStringLiteral("--convert-to"),
        QString::fromUtf8(filter.data(), static_cast<qsizetype>(filter.size())),
        QStringLiteral("--outdir"),
        toQString(outputDirectory),
        toQString(std::filesystem::absolute(source)),
    });

    std::cerr << "LibreOffice conversion: " << source << " -> " << result.output
              << " using filter " << filter << "\n";
    process.start();
    if (!process.waitForStarted(conversionTimeoutMilliseconds)) {
        std::cerr << "LibreOffice failed to start: "
                  << process.errorString().toStdString() << "\n";
        return result;
    }
    if (!process.waitForFinished(conversionTimeoutMilliseconds)) {
        process.kill();
        process.waitForFinished(5000);
        std::cerr << "LibreOffice conversion timed out after "
                  << conversionTimeoutMilliseconds << " ms\n";
        std::cerr << "stdout:\n" << process.readAllStandardOutput().toStdString()
                  << "\nstderr:\n" << process.readAllStandardError().toStdString()
                  << "\n";
        return result;
    }

    const auto standardOutput = process.readAllStandardOutput().toStdString();
    const auto standardError = process.readAllStandardError().toStdString();
    std::cerr << "LibreOffice exit code: " << process.exitCode() << "\n";
    if (!standardOutput.empty()) {
        std::cerr << "stdout:\n" << standardOutput << "\n";
    }
    if (!standardError.empty()) {
        std::cerr << "stderr:\n" << standardError << "\n";
    }

    const bool normalExit = process.exitStatus() == QProcess::NormalExit
        && process.exitCode() == 0;
    const bool hasOutput = std::filesystem::is_regular_file(result.output)
        && std::filesystem::file_size(result.output) > 0;
    if (!normalExit) {
        std::cerr << "LibreOffice conversion failed for " << source << "\n";
    } else if (!hasOutput) {
        std::cerr << "LibreOffice reported success without producing "
                  << result.output << "\n";
    }
    result.succeeded = normalExit && hasOutput;
    return result;
}

void writeDocument(
    const WordDocument& document,
    const std::filesystem::path& destination)
{
    const auto written = WordDocumentWriter{}.write(document, destination);
    printDiagnostics("write " + destination.string(), written.diagnostics);
    expect(!written.hasErrors(), "the library writes the interoperability source");
    expect(std::filesystem::is_regular_file(destination)
               && std::filesystem::file_size(destination) > 0,
           "the interoperability source is non-empty");
}

void expectInteroperableModel(
    const std::filesystem::path& source,
    std::string_view expectation)
{
    const auto read = WordDocumentReader{}.read(source);
    printDiagnostics("read " + source.string(), read.diagnostics);
    expect(!read.hasErrors(), "the library reopens the LibreOffice conversion");
    expect(read.document.plainText().find(unicodeBody) != std::string::npos,
           expectation);
    expect(read.document.metadata().contains("Title")
               && read.document.metadata().at("Title")
                    == "LibreOffice ODF interoperability",
           "document title survives the LibreOffice conversion");

    const WordParagraph* heading = nullptr;
    const WordParagraph* body = nullptr;
    const WordParagraph* numbered = nullptr;
    const WordTable* table = nullptr;
    for (const auto& block : read.document.blocks()) {
        if (const auto* paragraph = std::get_if<WordParagraph>(&block)) {
            if (paragraph->plainText() == "Interoperability heading") {
                heading = paragraph;
            } else if (paragraph->plainText().find(unicodeBody) != std::string::npos) {
                body = paragraph;
            } else if (paragraph->plainText() == "Interoperability list item") {
                numbered = paragraph;
            }
        } else {
            table = &std::get<WordTable>(block);
        }
    }
    expect(heading != nullptr, "heading text survives the LibreOffice conversion");
    expect(heading->properties.alignment == WordParagraphAlignment::center,
           "heading alignment survives the LibreOffice conversion");
    expect(std::ranges::any_of(heading->runs, [](const WordRun& run) {
               return run.properties.bold && run.properties.color == "1F4E78";
           }),
           "heading run formatting survives the LibreOffice conversion");
    expect(body != nullptr && std::ranges::all_of(body->runs, [](const WordRun& run) {
               return run.properties.italic;
           }),
           "body run formatting survives the LibreOffice conversion");
    expect(numbered != nullptr && numbered->properties.numberingId.has_value(),
           "list membership survives the LibreOffice conversion");
    expect(numbered->properties.numberingLevel == 1,
           "nested list level survives the LibreOffice conversion");
    expect(table != nullptr && table->rows.size() == 1
               && table->rows.front().cells.size() == 2
               && table->rows.front().cells.back().paragraphs.front().plainText() == "ODF",
           "table geometry and cell text survive the LibreOffice conversion");
    const auto& section = read.document.section();
    expect(std::abs(section.pageWidthTwips - 12240) <= 2
               && std::abs(section.pageHeightTwips - 15840) <= 2,
           "page size survives the LibreOffice conversion");
}

} // namespace

int main()
{
    const std::filesystem::path outputDirectory{TEST_OUTPUT_DIR};
    std::filesystem::create_directories(outputDirectory);

    QTemporaryDir conversionDirectory(
        toQString(outputDirectory / "odf-libreoffice-interop-XXXXXX"));
    expect(conversionDirectory.isValid(),
           "a temporary LibreOffice conversion directory is available");
    const std::filesystem::path conversionOutput{
        conversionDirectory.path().toStdString()};

    const auto document = sampleDocument();
    const auto odtSource = outputDirectory / "library-generated-for-docx.odt";
    const auto fodtSource = outputDirectory / "library-generated-for-pdf.fodt";
    const auto docxSource = outputDirectory / "library-generated-for-odf.docx";
    writeDocument(document, odtSource);
    writeDocument(document, fodtSource);
    writeDocument(document, docxSource);

    const auto convertedDocx = convertWithLibreOffice(
        odtSource, conversionOutput, "docx:Office Open XML Text", ".docx");
    expect(convertedDocx.succeeded,
           "LibreOffice converts the library-generated ODT to DOCX");
    expectInteroperableModel(
        convertedDocx.output,
        "Unicode body survives library ODT to LibreOffice DOCX conversion");

    const auto convertedPdf = convertWithLibreOffice(
        fodtSource, conversionOutput, "pdf:writer_pdf_Export", ".pdf");
    expect(convertedPdf.succeeded,
           "LibreOffice converts the library-generated FODT to a non-empty PDF");

    const auto convertedOdt = convertWithLibreOffice(
        docxSource, conversionOutput, "odt:writer8", ".odt");
    expect(convertedOdt.succeeded,
           "LibreOffice converts the library-generated DOCX to ODT");
    expectInteroperableModel(
        convertedOdt.output,
        "Unicode body survives library DOCX to LibreOffice ODT conversion");

    const auto convertedFodt = convertWithLibreOffice(
        docxSource,
        conversionOutput,
        "fodt:OpenDocument Text Flat XML",
        ".fodt");
    expect(convertedFodt.succeeded,
           "LibreOffice converts the library-generated DOCX to FODT");
    expectInteroperableModel(
        convertedFodt.output,
        "Unicode body survives library DOCX to LibreOffice FODT conversion");
}
