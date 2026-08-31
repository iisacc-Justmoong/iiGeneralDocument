#include "TestSupport.h"
#include "Word/WordDocumentReader.h"
#include "Word/WordDocumentWriter.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <variant>

using namespace ii::document;

namespace {

WordDocument sampleDocument()
{
    WordDocument document;
    document.metadata()["Title"] = "DOCX round trip";
    document.metadata()["Author"] = "iiGeneralDocument";

    WordParagraph heading;
    heading.properties.styleId = "Heading1";
    heading.properties.alignment = WordParagraphAlignment::center;
    heading.runs.push_back({"Word 왕복", {.bold = true, .fontFamily = "Arial",
                                          .eastAsiaFontFamily = "Nanum Gothic",
                                          .fontSizePoints = 16.0, .color = "1F4E78"}});
    document.appendParagraph(std::move(heading));

    WordParagraph body;
    body.runs.push_back({"첫 번째 런 ", {.italic = true}});
    body.runs.push_back({"second run\twith tab\nand break", {.underline = true}});
    document.appendParagraph(std::move(body));

    WordTable table;
    for (const auto& values : {std::pair{"Name", "Value"},
                               std::pair{"format", "docx"}}) {
        WordTableRow row;
        for (const auto* value : {values.first, values.second}) {
            WordTableCell cell;
            WordParagraph paragraph;
            paragraph.runs.push_back({value});
            cell.paragraphs.push_back(std::move(paragraph));
            row.cells.push_back(std::move(cell));
        }
        table.rows.push_back(std::move(row));
    }
    document.appendTable(std::move(table));
    return document;
}

} // namespace

int main()
{
    const std::filesystem::path outputDirectory{IIGENERALDOCUMENT_TEST_OUTPUT_DIR};
    std::filesystem::create_directories(outputDirectory);
    const auto createdPath = outputDirectory / "word-roundtrip.docx";
    const auto rewrittenPath = outputDirectory / "word-roundtrip-rewritten.docx";

    auto document = sampleDocument();
    const auto written = WordDocumentWriter{}.write(document, createdPath);
    expect(!written.hasErrors(), "a DOCX document is written");
    expect(std::filesystem::file_size(createdPath) > 0, "written DOCX is non-empty");

    std::ifstream package(createdPath, std::ios::binary);
    char signature[2]{};
    package.read(signature, 2);
    expect(signature[0] == 'P' && signature[1] == 'K', "DOCX is an OOXML ZIP package");

    auto read = WordDocumentReader{}.read(createdPath);
    expect(!read.hasErrors(), "written DOCX can be reopened");
    expect(read.document.metadata().at("Title") == "DOCX round trip",
           "core title metadata round-trips");
    expect(read.document.metadata().at("Author") == "iiGeneralDocument",
           "core author metadata round-trips");
    expect(read.document.blocks().size() == 3, "paragraph and table order round-trips");

    const auto& heading = std::get<WordParagraph>(read.document.blocks().front());
    expect(heading.plainText() == "Word 왕복", "UTF-8 text round-trips");
    expect(heading.properties.styleId == "Heading1", "paragraph style round-trips");
    expect(heading.properties.alignment == WordParagraphAlignment::center,
           "paragraph alignment round-trips");
    expect(heading.runs.front().properties.bold, "run emphasis round-trips");
    expect(heading.runs.front().properties.fontSizePoints == 16.0,
           "run font size round-trips");
    expect(heading.runs.front().properties.eastAsiaFontFamily == "Nanum Gothic",
           "East Asian run font round-trips independently");

    const auto& table = std::get<WordTable>(read.document.blocks().back());
    expect(table.rows.size() == 2 && table.rows.front().cells.size() == 2,
           "table geometry round-trips");
    expect(table.rows.back().cells.back().paragraphs.front().plainText() == "docx",
           "table-cell text round-trips");

    std::get<WordParagraph>(read.document.blocks().front()).runs.front().text = "수정된 제목";
    const auto rewritten = WordDocumentWriter{}.write(read.document, rewrittenPath);
    expect(!rewritten.hasErrors(), "a reopened DOCX can be rewritten");
    auto reread = WordDocumentReader{}.read(rewrittenPath);
    expect(std::get<WordParagraph>(reread.document.blocks().front()).plainText()
               == "수정된 제목",
           "an edited run persists after reopening");

    WordReadOptions tinyPartLimit;
    tinyPartLimit.maximumXmlPartBytes = 1;
    const auto limited = WordDocumentReader{}.read(createdPath, tinyPartLimit);
    expect(limited.hasErrors(), "DOCX XML parts respect the configured size limit");

    const auto malformedPath = outputDirectory / "malformed.docx";
    std::ofstream malformedFile(malformedPath, std::ios::binary | std::ios::trunc);
    malformedFile << "not an OOXML package";
    malformedFile.close();
    const auto malformed = WordDocumentReader{}.read(malformedPath);
    expect(malformed.hasErrors(), "malformed DOCX packages fail closed");

    const auto unsupported = WordDocumentWriter{}.write(document, outputDirectory / "bad.txt");
    expect(unsupported.hasErrors(), "unsupported word extensions fail closed");

    WordDocument invalidDocument;
    invalidDocument.appendTable({});
    const auto invalid = WordDocumentWriter{}.write(
        invalidDocument, outputDirectory / "invalid-table.docx");
    expect(invalid.hasErrors(), "structurally invalid Word tables fail before writing");
}
