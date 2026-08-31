#include "TestSupport.h"
#include "Word/WordDocumentReader.h"
#include "Word/WordDocumentWriter.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace ii::document;

int main()
{
    const std::filesystem::path outputDirectory{IIGENERALDOCUMENT_TEST_OUTPUT_DIR};
    std::filesystem::create_directories(outputDirectory);
    const auto docPath = outputDirectory / "legacy-roundtrip.doc";

    WordDocument document;
    document.metadata()["Title"] = "Legacy DOC round trip";
    WordParagraph paragraph;
    paragraph.runs.push_back({"Legacy DOC 읽기와 쓰기", {.bold = true}});
    document.appendParagraph(std::move(paragraph));

    WordWriteOptions writeOptions;
    writeOptions.libreOfficeExecutable = IIGENERALDOCUMENT_TEST_SOFFICE;
    const auto written = WordDocumentWriter{}.write(document, docPath, writeOptions);
    for (const auto& item : written.diagnostics) {
        std::cerr << item.code << ": " << item.message << " [" << item.context << "]\n";
    }
    expect(!written.hasErrors(), "LibreOffice backend writes binary DOC");
    expect(std::filesystem::file_size(docPath) > 0, "binary DOC is non-empty");

    std::ifstream input(docPath, std::ios::binary);
    unsigned char signature[8]{};
    input.read(reinterpret_cast<char*>(signature), 8);
    const unsigned char oleSignature[8]{0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1};
    expect(std::equal(std::begin(signature), std::end(signature), std::begin(oleSignature)),
           "legacy DOC uses the OLE compound-file signature");

    WordReadOptions readOptions;
    readOptions.libreOfficeExecutable = IIGENERALDOCUMENT_TEST_SOFFICE;
    const auto read = WordDocumentReader{}.read(docPath, readOptions);
    for (const auto& item : read.diagnostics) {
        std::cerr << item.code << ": " << item.message << " [" << item.context << "]\n";
    }
    expect(!read.hasErrors(), "LibreOffice backend reads binary DOC");
    expect(read.document.plainText().find("Legacy DOC 읽기와 쓰기") != std::string::npos,
           "legacy DOC Unicode text survives conversion");

    WordReadOptions unavailable;
    unavailable.libreOfficeExecutable = outputDirectory / "missing-soffice";
    const auto missing = WordDocumentReader{}.read(docPath, unavailable);
    expect(missing.hasErrors(), "an unavailable DOC converter fails closed");
}
