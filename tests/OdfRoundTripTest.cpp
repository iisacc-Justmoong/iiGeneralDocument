#include "TestSupport.h"
#include "Word/WordDocumentReader.h"
#include "Word/WordDocumentWriter.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

using namespace ii::document;

namespace {

constexpr std::string_view odtMimeType =
    "application/vnd.oasis.opendocument.text";

void printDiagnostics(const std::vector<Diagnostic>& diagnostics)
{
    for (const auto& item : diagnostics) {
        std::cerr << item.code << ": " << item.message << " [" << item.context << "]\n";
    }
}

WordDocument sampleDocument()
{
    WordDocument document;
    document.metadata()["Title"] = "ODF CRUD round trip";
    document.metadata()["Author"] = "iiGeneralDocument";

    WordParagraph heading;
    heading.properties.styleId = "Heading1";
    heading.properties.alignment = WordParagraphAlignment::center;
    heading.runs.push_back({
        "공개 문서 형식",
        {.bold = true,
         .fontFamily = "Liberation Sans",
         .eastAsiaFontFamily = "Noto Sans CJK KR",
         .fontSizePoints = 18.0,
         .color = "1F4E78"},
    });
    document.appendParagraph(std::move(heading));

    WordParagraph body;
    body.runs.push_back({"첫 번째 런 ", {.italic = true}});
    body.runs.push_back({"second run\twith tab\nand break", {.underline = true}});
    document.appendParagraph(std::move(body));

    WordParagraph numbered;
    numbered.properties.numberingId = 7;
    numbered.properties.numberingLevel = 1;
    numbered.runs.push_back({"번호가 있는 문단", {}});
    document.appendParagraph(std::move(numbered));

    WordParagraph removable;
    removable.runs.push_back({"remove this paragraph", {}});
    document.appendParagraph(std::move(removable));

    WordTable table;
    for (const auto& values : {std::pair{"Name", "Value"},
                               std::pair{"format", "OpenDocument"}}) {
        WordTableRow row;
        for (const auto* value : {values.first, values.second}) {
            WordTableCell cell;
            WordParagraph paragraph;
            paragraph.runs.push_back({value, {}});
            cell.paragraphs.push_back(std::move(paragraph));
            row.cells.push_back(std::move(cell));
        }
        table.rows.push_back(std::move(row));
    }
    document.appendTable(std::move(table));
    return document;
}

const WordParagraph* findParagraph(const WordDocument& document, std::string_view text)
{
    for (const auto& block : document.blocks()) {
        if (const auto* paragraph = std::get_if<WordParagraph>(&block);
            paragraph != nullptr && paragraph->plainText() == text) {
            return paragraph;
        }
    }
    return nullptr;
}

WordParagraph* findParagraph(WordDocument& document, std::string_view text)
{
    for (auto& block : document.blocks()) {
        if (auto* paragraph = std::get_if<WordParagraph>(&block);
            paragraph != nullptr && paragraph->plainText() == text) {
            return paragraph;
        }
    }
    return nullptr;
}

const WordTable* findTable(const WordDocument& document)
{
    for (const auto& block : document.blocks()) {
        if (const auto* table = std::get_if<WordTable>(&block)) {
            return table;
        }
    }
    return nullptr;
}

std::uint16_t readLittleEndian16(
    const std::array<unsigned char, 30>& header, std::size_t offset)
{
    return static_cast<std::uint16_t>(header[offset])
        | static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(header[offset + 1]) << 8U);
}

void verifyOdtMimetypeEntry(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    expect(input.is_open(), "ODT package can be opened for ZIP contract inspection");

    std::array<unsigned char, 30> header{};
    input.read(reinterpret_cast<char*>(header.data()),
               static_cast<std::streamsize>(header.size()));
    expect(input.gcount() == static_cast<std::streamsize>(header.size()),
           "ODT starts with a complete ZIP local-file header");
    expect(header[0] == 0x50U && header[1] == 0x4bU
               && header[2] == 0x03U && header[3] == 0x04U,
           "ODT starts with a ZIP local-file entry");

    const auto compressionMethod = readLittleEndian16(header, 8);
    const auto filenameLength = readLittleEndian16(header, 26);
    const auto extraLength = readLittleEndian16(header, 28);
    expect(compressionMethod == 0,
           "the first ODT ZIP entry is stored without compression");
    expect(extraLength == 0,
           "the first ODT ZIP entry has no local-header extra field");

    std::string filename(filenameLength, '\0');
    input.read(filename.data(), static_cast<std::streamsize>(filename.size()));
    expect(input.good(), "the first ODT ZIP entry name is readable");
    expect(filename == "mimetype", "mimetype is the first ODT ZIP entry");

    input.seekg(static_cast<std::streamoff>(extraLength), std::ios::cur);
    expect(input.good(), "the ODT mimetype entry extra field is readable");

    std::string mediaType(odtMimeType.size(), '\0');
    input.read(mediaType.data(), static_cast<std::streamsize>(mediaType.size()));
    expect(input.gcount() == static_cast<std::streamsize>(mediaType.size()),
           "the complete ODT media type is readable");
    expect(mediaType == odtMimeType, "the ODT mimetype entry has the standard media type");
}

void verifyFlatOdfEnvelope(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    expect(input.is_open(), "FODT can be opened for flat-XML inspection");
    const std::string xml{
        std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    expect(!xml.starts_with("PK"), "FODT is flat XML rather than a ZIP package");
    expect(xml.find("office:document") != std::string::npos,
           "FODT contains an OpenDocument document root");
    expect(xml.find(odtMimeType) != std::string::npos,
           "FODT declares the OpenDocument text media type");
}

void verifyInitialRead(const WordDocument& document)
{
    expect(document.metadata().at("Title") == "ODF CRUD round trip",
           "ODF title metadata round-trips");
    expect(document.metadata().at("Author") == "iiGeneralDocument",
           "ODF author metadata round-trips");

    const auto* heading = findParagraph(document, "공개 문서 형식");
    expect(heading != nullptr, "ODF Unicode heading round-trips");
    expect(heading->properties.styleId == "Heading1",
           "ODF heading style round-trips");
    expect(heading->properties.alignment == WordParagraphAlignment::center,
           "ODF paragraph alignment round-trips");
    expect(heading->runs.size() == 1, "ODF heading retains its independently editable run");
    expect(heading->runs.front().properties.bold,
           "ODF bold run formatting round-trips");
    expect(heading->runs.front().properties.fontFamily == "Liberation Sans",
           "ODF Latin run font round-trips");
    expect(heading->runs.front().properties.eastAsiaFontFamily == "Noto Sans CJK KR",
           "ODF East Asian run font round-trips");
    expect(heading->runs.front().properties.fontSizePoints == 18.0,
           "ODF run font size round-trips");
    expect(heading->runs.front().properties.color == "1F4E78",
           "ODF run color round-trips");

    const auto* body = findParagraph(document, "첫 번째 런 second run\twith tab\nand break");
    expect(body != nullptr && body->runs.size() == 2,
           "ODF preserves Unicode text, tabs, breaks, and run boundaries");
    expect(body->runs.front().properties.italic,
           "ODF italic run formatting round-trips");
    expect(body->runs.back().properties.underline,
           "ODF underline run formatting round-trips");

    const auto* numbered = findParagraph(document, "번호가 있는 문단");
    expect(numbered != nullptr && numbered->properties.numberingId.has_value(),
           "ODF numbered paragraph remains numbered");
    expect(numbered->properties.numberingLevel == 1,
           "ODF numbered paragraph level round-trips");

    const auto* table = findTable(document);
    expect(table != nullptr && table->rows.size() == 2
               && table->rows.front().cells.size() == 2,
           "ODF table geometry round-trips");
    expect(table->rows.back().cells.back().paragraphs.front().plainText() == "OpenDocument",
           "ODF table-cell text round-trips");
}

void editAndRewrite(
    WordDocument& document,
    const std::filesystem::path& rewrittenPath)
{
    auto* heading = findParagraph(document, "공개 문서 형식");
    expect(heading != nullptr, "the reopened ODF heading is editable");
    heading->runs.front().text = "수정된 공개 문서 제목";

    const auto removable = std::find_if(
        document.blocks().begin(), document.blocks().end(), [](const WordBlock& block) {
            const auto* paragraph = std::get_if<WordParagraph>(&block);
            return paragraph != nullptr && paragraph->plainText() == "remove this paragraph";
        });
    expect(removable != document.blocks().end(),
           "the ODF paragraph selected for deletion is addressable");
    document.blocks().erase(removable);

    WordParagraph created;
    created.runs.push_back({"rewrite-created paragraph", {.bold = true}});
    document.appendParagraph(std::move(created));

    const auto rewritten = WordDocumentWriter{}.write(document, rewrittenPath);
    printDiagnostics(rewritten.diagnostics);
    expect(!rewritten.hasErrors(), "an edited ODF document can be rewritten");
}

void verifyRewrittenRead(const WordDocument& document)
{
    expect(findParagraph(document, "수정된 공개 문서 제목") != nullptr,
           "ODF update persists after rewrite and reopen");
    expect(findParagraph(document, "remove this paragraph") == nullptr,
           "ODF vector deletion persists after rewrite and reopen");
    const auto* created = findParagraph(document, "rewrite-created paragraph");
    expect(created != nullptr && created->runs.front().properties.bold,
           "ODF vector creation persists after rewrite and reopen");
    expect(findTable(document) != nullptr,
           "unrelated ODF table content survives CRUD rewrite");
}

void roundTripFormat(
    const std::filesystem::path& outputDirectory,
    std::string_view extension)
{
    const std::string suffix(extension);
    const auto createdPath = outputDirectory / ("odf-roundtrip" + suffix);
    const auto rewrittenPath = outputDirectory / ("odf-roundtrip-rewritten" + suffix);

    auto document = sampleDocument();
    const auto written = WordDocumentWriter{}.write(document, createdPath);
    printDiagnostics(written.diagnostics);
    expect(!written.hasErrors(), "an OpenDocument text document is created");
    expect(std::filesystem::file_size(createdPath) > 0,
           "the created OpenDocument text file is non-empty");

    if (extension == ".odt") {
        verifyOdtMimetypeEntry(createdPath);
    } else {
        verifyFlatOdfEnvelope(createdPath);
    }

    auto read = WordDocumentReader{}.read(createdPath);
    printDiagnostics(read.diagnostics);
    expect(!read.hasErrors(), "the created OpenDocument text file can be reopened");
    verifyInitialRead(read.document);

    editAndRewrite(read.document, rewrittenPath);
    auto reread = WordDocumentReader{}.read(rewrittenPath);
    printDiagnostics(reread.diagnostics);
    expect(!reread.hasErrors(), "the rewritten OpenDocument text file can be reopened");
    verifyRewrittenRead(reread.document);

    WordReadOptions tinyPartLimit;
    tinyPartLimit.maximumXmlPartBytes = 1;
    const auto limited = WordDocumentReader{}.read(createdPath, tinyPartLimit);
    expect(limited.hasErrors(), "OpenDocument XML respects the configured size limit");
}

void malformedInputsFailClosed(const std::filesystem::path& outputDirectory)
{
    const auto malformedOdt = outputDirectory / "malformed.odt";
    std::ofstream odt(malformedOdt, std::ios::binary | std::ios::trunc);
    odt << "not an OpenDocument ZIP package";
    odt.close();
    const auto odtRead = WordDocumentReader{}.read(malformedOdt);
    expect(odtRead.hasErrors(), "a malformed ODT package fails closed");

    const auto malformedFodt = outputDirectory / "malformed.fodt";
    std::ofstream fodt(malformedFodt, std::ios::binary | std::ios::trunc);
    fodt << "<office:document>";
    fodt.close();
    const auto fodtRead = WordDocumentReader{}.read(malformedFodt);
    expect(fodtRead.hasErrors(), "malformed FODT XML fails closed");

    constexpr std::string_view flatRoot =
        "<office:document "
        "xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" "
        "xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\" "
        "xmlns:table=\"urn:oasis:names:tc:opendocument:xmlns:table:1.0\" "
        "office:version=\"1.3\" ";
    constexpr std::string_view emptyBody =
        "><office:body><office:text/></office:body></office:document>";

    const auto wrongMimeFodt = outputDirectory / "wrong-mimetype.fodt";
    std::ofstream wrongMime(wrongMimeFodt, std::ios::binary | std::ios::trunc);
    wrongMime << flatRoot << "office:mimetype=\"application/xml\"" << emptyBody;
    wrongMime.close();
    expect(WordDocumentReader{}.read(wrongMimeFodt).hasErrors(),
           "FODT with the wrong root media type fails closed");

    const auto dtdFodt = outputDirectory / "dtd.fodt";
    std::ofstream dtd(dtdFodt, std::ios::binary | std::ios::trunc);
    dtd << "<!DOCTYPE office:document [<!ENTITY payload \"not allowed\">]>"
        << flatRoot << "office:mimetype=\"" << odtMimeType << "\""
        << emptyBody;
    dtd.close();
    expect(WordDocumentReader{}.read(dtdFodt).hasErrors(),
           "FODT containing a DTD fails closed");

    const auto repeatedFodt = outputDirectory / "repetition-limit.fodt";
    std::ofstream repeated(repeatedFodt, std::ios::binary | std::ios::trunc);
    repeated << flatRoot << "office:mimetype=\"" << odtMimeType << "\">"
        << "<office:body><office:text><table:table>"
        << "<table:table-row table:number-rows-repeated=\"1000001\">"
        << "<table:table-cell><text:p>x</text:p></table:table-cell>"
        << "</table:table-row></table:table></office:text></office:body>"
        << "</office:document>";
    repeated.close();
    expect(WordDocumentReader{}.read(repeatedFodt).hasErrors(),
           "FODT repeated rows respect the expansion safety limit");
}

WordParagraph numberedParagraph(std::string text, int identifier, int level)
{
    WordParagraph paragraph;
    paragraph.properties.numberingId = identifier;
    paragraph.properties.numberingLevel = level;
    paragraph.runs.push_back({std::move(text), {}});
    return paragraph;
}

std::size_t occurrenceCount(std::string_view value, std::string_view needle)
{
    std::size_t count = 0;
    std::size_t offset = 0;
    while ((offset = value.find(needle, offset)) != std::string_view::npos) {
        ++count;
        offset += needle.size();
    }
    return count;
}

void numberedListsPreserveStructure(const std::filesystem::path& outputDirectory)
{
    WordDocument document;
    document.appendParagraph(numberedParagraph("one", 42, 0));
    document.appendParagraph(numberedParagraph("one-one", 42, 1));
    document.appendParagraph(numberedParagraph("one-two", 42, 1));
    document.appendParagraph(numberedParagraph("two", 42, 0));

    const auto path = outputDirectory / "grouped-list.fodt";
    const auto written = WordDocumentWriter{}.write(document, path);
    printDiagnostics(written.diagnostics);
    expect(!written.hasErrors(), "a multi-level list can be written to FODT");

    std::ifstream input(path, std::ios::binary);
    const std::string xml{
        std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    expect(occurrenceCount(xml, "<text:list ") == 2,
           "adjacent list paragraphs share one outer list and one nested list");
    expect(occurrenceCount(xml, "<text:list-item") == 4,
           "the grouped list writes one list item per numbered paragraph");

    const auto read = WordDocumentReader{}.read(path);
    printDiagnostics(read.diagnostics);
    expect(!read.hasErrors(), "the grouped multi-level FODT list can be reopened");
    expect(read.document.blocks().size() == 4,
           "the grouped list reopens as four independently editable paragraphs");
    std::optional<int> identifier;
    const std::array expectedLevels{0, 1, 1, 0};
    for (std::size_t index = 0; index < expectedLevels.size(); ++index) {
        const auto* paragraph = std::get_if<WordParagraph>(&read.document.blocks()[index]);
        expect(paragraph != nullptr && paragraph->properties.numberingId.has_value(),
               "each reopened list paragraph remains numbered");
        if (!identifier) {
            identifier = paragraph->properties.numberingId;
        }
        expect(paragraph->properties.numberingId == identifier,
               "nested items inherit the outer list instance identifier");
        expect(paragraph->properties.numberingLevel == expectedLevels[index],
               "the reopened list paragraph retains its nesting level");
    }
}

void listInstancesAndContinuationAreDistinct(
    const std::filesystem::path& outputDirectory)
{
    const auto path = outputDirectory / "list-instances.fodt";
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output
        << "<office:document "
        << "xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" "
        << "xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\" "
        << "office:version=\"1.3\" office:mimetype=\"" << odtMimeType << "\">"
        << "<office:body><office:text>"
        << "<text:list text:style-name=\"Shared\" xml:id=\"first\">"
        << "<text:list-item><text:p>a</text:p></text:list-item></text:list>"
        << "<text:p>separator</text:p>"
        << "<text:list text:style-name=\"Shared\">"
        << "<text:list-item><text:p>b</text:p></text:list-item></text:list>"
        << "<text:list text:style-name=\"Shared\" text:continue-list=\"first\">"
        << "<text:list-item><text:p>c</text:p></text:list-item></text:list>"
        << "</office:text></office:body></office:document>";
    output.close();

    const auto read = WordDocumentReader{}.read(path);
    printDiagnostics(read.diagnostics);
    expect(!read.hasErrors(), "independent and continued ODF lists can be read");
    const auto* first = findParagraph(read.document, "a");
    const auto* independent = findParagraph(read.document, "b");
    const auto* continued = findParagraph(read.document, "c");
    expect(first != nullptr && independent != nullptr && continued != nullptr,
           "all list instances remain editable paragraphs");
    expect(first->properties.numberingId != independent->properties.numberingId,
           "separate top-level lists do not merge merely because their style matches");
    expect(first->properties.numberingId == continued->properties.numberingId,
           "text:continue-list reconnects the referenced list counter domain");
}

void continueNumberingRequiresCompatibleStyle(
    const std::filesystem::path& outputDirectory)
{
    const auto path = outputDirectory / "list-continuation-rules.fodt";
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output
        << "<office:document "
        << "xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" "
        << "xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\" "
        << "office:version=\"1.3\" office:mimetype=\"" << odtMimeType << "\">"
        << "<office:body><office:text>"
        << "<text:list text:style-name=\"StyleA\" xml:id=\"first\">"
        << "<text:list-item><text:p>a</text:p></text:list-item></text:list>"
        << "<text:list text:style-name=\"StyleA\" text:continue-numbering=\"true\">"
        << "<text:list-item><text:p>b</text:p></text:list-item></text:list>"
        << "<text:list text:style-name=\"StyleB\" text:continue-numbering=\"true\">"
        << "<text:list-item><text:p>c</text:p></text:list-item></text:list>"
        << "<text:list text:style-name=\"StyleB\" text:continue-list=\"missing\" "
           "text:continue-numbering=\"true\">"
        << "<text:list-item><text:p>d</text:p></text:list-item></text:list>"
        << "</office:text></office:body></office:document>";
    output.close();

    const auto read = WordDocumentReader{}.read(path);
    printDiagnostics(read.diagnostics);
    expect(!read.hasErrors(), "ODF list continuation compatibility rules are readable");
    const auto* a = findParagraph(read.document, "a");
    const auto* b = findParagraph(read.document, "b");
    const auto* c = findParagraph(read.document, "c");
    const auto* d = findParagraph(read.document, "d");
    expect(a != nullptr && b != nullptr && c != nullptr && d != nullptr,
           "all continuation-rule paragraphs remain editable");
    expect(a->properties.numberingId == b->properties.numberingId,
           "continue-numbering reuses the previous list only for the same style");
    expect(b->properties.numberingId != c->properties.numberingId,
           "continue-numbering does not merge lists with different styles");
    expect(c->properties.numberingId != d->properties.numberingId,
           "an unresolved explicit continue-list target does not fall back to continue-numbering");
}

void implicitListStylesControlContinuation(
    const std::filesystem::path& outputDirectory)
{
    const auto path = outputDirectory / "implicit-list-style-continuation.fodt";
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output
        << "<office:document "
        << "xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" "
        << "xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\" "
        << "xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\" "
        << "office:version=\"1.3\" office:mimetype=\"" << odtMimeType << "\">"
        << "<office:styles>"
        << "<style:style style:name=\"ParagraphA\" style:family=\"paragraph\" "
           "style:list-style-name=\"ListA\"/>"
        << "<style:style style:name=\"ParagraphCleared\" style:family=\"paragraph\" "
           "style:parent-style-name=\"ParagraphA\" style:list-style-name=\"\"/>"
        << "<style:style style:name=\"ParagraphB\" style:family=\"paragraph\" "
           "style:list-style-name=\"ListB\"/>"
        << "</office:styles><office:body><office:text>"
        << "<text:list><text:list-item><text:p text:style-name=\"ParagraphA\">a</text:p>"
           "</text:list-item></text:list>"
        << "<text:list text:continue-numbering=\"true\"><text:list-item>"
           "<text:p text:style-name=\"ParagraphCleared\">cleared</text:p>"
           "</text:list-item></text:list>"
        << "<text:list text:continue-numbering=\"true\"><text:list-item>"
           "<text:p text:style-name=\"ParagraphB\">b</text:p>"
           "</text:list-item></text:list>"
        << "<text:list text:continue-numbering=\"true\"><text:list-item>"
           "<text:p text:style-name=\"ParagraphB\">c</text:p>"
           "</text:list-item></text:list>"
        << "</office:text></office:body></office:document>";
    output.close();

    const auto read = WordDocumentReader{}.read(path);
    printDiagnostics(read.diagnostics);
    expect(!read.hasErrors(), "implicit ODF list styles are readable");
    const auto* a = findParagraph(read.document, "a");
    const auto* cleared = findParagraph(read.document, "cleared");
    const auto* b = findParagraph(read.document, "b");
    const auto* c = findParagraph(read.document, "c");
    expect(a != nullptr && cleared != nullptr && b != nullptr && c != nullptr,
           "implicit-style list paragraphs remain editable");
    expect(a->properties.numberingId != cleared->properties.numberingId,
           "an explicit empty list-style-name cancels inherited list style");
    expect(cleared->properties.numberingId != b->properties.numberingId,
           "different paragraph-derived list styles do not continue one counter");
    expect(b->properties.numberingId == c->properties.numberingId,
           "matching paragraph-derived list styles can continue one counter");
}

void listItemContinuationParagraphsRoundTrip(
    const std::filesystem::path& outputDirectory)
{
    const auto source = outputDirectory / "multi-paragraph-list-item.fodt";
    std::ofstream output(source, std::ios::binary | std::ios::trunc);
    output
        << "<office:document "
        << "xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" "
        << "xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\" "
        << "office:version=\"1.3\" office:mimetype=\"" << odtMimeType << "\">"
        << "<office:body><office:text><text:list text:style-name=\"Shared\">"
        << "<text:list-item><text:p>first</text:p><text:p>second</text:p></text:list-item>"
        << "</text:list></office:text></office:body></office:document>";
    output.close();

    const auto read = WordDocumentReader{}.read(source);
    printDiagnostics(read.diagnostics);
    expect(!read.hasErrors(), "a multi-paragraph ODF list item is readable");
    const auto* first = findParagraph(read.document, "first");
    const auto* second = findParagraph(read.document, "second");
    expect(first != nullptr && second != nullptr,
           "both list-item paragraphs remain independently editable");
    expect(first->properties.numberingId == second->properties.numberingId,
           "list-item paragraphs retain one numbering identity");
    expect(!first->properties.numberingContinuation
               && second->properties.numberingContinuation,
           "only later paragraphs are marked as continuations of the same list item");

    const auto rewritten = outputDirectory / "multi-paragraph-list-item-rewritten.fodt";
    const auto written = WordDocumentWriter{}.write(read.document, rewritten);
    printDiagnostics(written.diagnostics);
    expect(!written.hasErrors(), "a multi-paragraph list item can be rewritten");
    std::ifstream input(rewritten, std::ios::binary);
    const std::string xml{
        std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    expect(occurrenceCount(xml, "<text:list-item") == 1,
           "rewriting keeps continuation paragraphs inside one list item");
    expect(occurrenceCount(xml, "<text:p") == 2,
           "rewriting keeps both paragraphs inside the list item");

    const auto reopened = WordDocumentReader{}.read(rewritten);
    printDiagnostics(reopened.diagnostics);
    expect(!reopened.hasErrors(), "the rewritten multi-paragraph list item reopens");
    const auto* reopenedSecond = findParagraph(reopened.document, "second");
    expect(reopenedSecond != nullptr
               && reopenedSecond->properties.numberingContinuation,
           "the continuation contract survives another ODF read");

    const auto docx = outputDirectory / "multi-paragraph-list-item.docx";
    const auto docxWritten = WordDocumentWriter{}.write(read.document, docx);
    printDiagnostics(docxWritten.diagnostics);
    expect(!docxWritten.hasErrors(),
           "a multi-paragraph ODF list item can be converted to DOCX");
    const auto docxRead = WordDocumentReader{}.read(docx);
    printDiagnostics(docxRead.diagnostics);
    expect(!docxRead.hasErrors(),
           "the converted multi-paragraph DOCX reopens");
    const auto* docxFirst = findParagraph(docxRead.document, "first");
    const auto* docxSecond = findParagraph(docxRead.document, "second");
    expect(docxFirst != nullptr && docxFirst->properties.numberingId
               && docxSecond != nullptr && !docxSecond->properties.numberingId,
           "DOCX conversion numbers the list item once and keeps its continuation unnumbered");
}

void styleCatalogKeysAreStructural(const std::filesystem::path& outputDirectory)
{
    WordDocument document;
    WordParagraph paragraph;
    paragraph.runs.push_back({
        "left", {.fontFamily = "A|B", .eastAsiaFontFamily = "C"}});
    paragraph.runs.push_back({
        "right", {.fontFamily = "A", .eastAsiaFontFamily = "B|C"}});
    document.appendParagraph(std::move(paragraph));

    const auto path = outputDirectory / "structural-style-keys.fodt";
    const auto written = WordDocumentWriter{}.write(document, path);
    printDiagnostics(written.diagnostics);
    expect(!written.hasErrors(), "structurally distinct ODF run styles can be written");
    const auto read = WordDocumentReader{}.read(path);
    printDiagnostics(read.diagnostics);
    expect(!read.hasErrors(), "structurally distinct ODF run styles can be reopened");
    const auto* reopened = findParagraph(read.document, "leftright");
    expect(reopened != nullptr && reopened->runs.size() == 2,
           "distinct style-key fields retain independent editable runs");
    expect(reopened->runs[0].properties.fontFamily == "A|B"
               && reopened->runs[0].properties.eastAsiaFontFamily == "C",
           "the first structural run-style key round-trips without delimiter collision");
    expect(reopened->runs[1].properties.fontFamily == "A"
               && reopened->runs[1].properties.eastAsiaFontFamily == "B|C",
           "the second structural run-style key round-trips without delimiter collision");
}

void whitespaceNormalizationCrossesInlineBoundaries(
    const std::filesystem::path& outputDirectory)
{
    const auto path = outputDirectory / "odf-inline-whitespace.fodt";
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output
        << "<office:document "
        << "xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" "
        << "xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\" "
        << "office:version=\"1.3\" office:mimetype=\"" << odtMimeType << "\">"
        << "<office:body><office:text>"
        << "<text:p>a <text:span>b</text:span> <text:span> c</text:span></text:p>"
        << "<text:p>  a  </text:p>"
        << "<text:p>a&#160;b</text:p>"
        << "</office:text></office:body></office:document>";
    output.close();

    const auto read = WordDocumentReader{}.read(path);
    printDiagnostics(read.diagnostics);
    expect(!read.hasErrors(), "ODF whitespace split by inline elements is readable");
    expect(findParagraph(read.document, "a b c") != nullptr,
           "ODF whitespace collapses once across adjacent inline XML tokens");
    expect(findParagraph(read.document, "a") != nullptr,
           "ODF leading and trailing collapsible spaces are removed");
    expect(findParagraph(read.document, "a\xC2\xA0" "b") != nullptr,
           "ODF non-breaking spaces remain Unicode text rather than ASCII spaces");
}

} // namespace

int main()
{
    const std::filesystem::path outputDirectory{IIGENERALDOCUMENT_TEST_OUTPUT_DIR};
    std::filesystem::create_directories(outputDirectory);

    roundTripFormat(outputDirectory, ".odt");
    roundTripFormat(outputDirectory, ".fodt");
    malformedInputsFailClosed(outputDirectory);
    numberedListsPreserveStructure(outputDirectory);
    listInstancesAndContinuationAreDistinct(outputDirectory);
    continueNumberingRequiresCompatibleStyle(outputDirectory);
    implicitListStylesControlContinuation(outputDirectory);
    listItemContinuationParagraphsRoundTrip(outputDirectory);
    styleCatalogKeysAreStructural(outputDirectory);
    whitespaceNormalizationCrossesInlineBoundaries(outputDirectory);
}
