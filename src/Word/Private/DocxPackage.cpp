#include "Codec/Private/Xml.h"
#include "Word/Private/ProviderZipSource.h"
#include "Word/Private/DocxPackage.h"

#include "Word/Private/AtomicFileCommit.h"


#include <zip.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <limits>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ii::document::detail {
namespace {

const Text& wordNamespace()
{
    static const Text value =
        Text("http://schemas.openxmlformats.org/wordprocessingml/2006/main");
    return value;
}

const Text& relationshipNamespace()
{
    static const Text value =
        Text("http://schemas.openxmlformats.org/package/2006/relationships");
    return value;
}

std::string toUtf8(const Text& value)
{
    const auto bytes = value.toUtf8();
    return {bytes.constData(), static_cast<std::size_t>(bytes.size())};
}

Text attribute(const XmlReader& xml, const char* localName)
{
    const auto requested = Text::fromLatin1(localName);
    for (const auto& item : xml.attributes()) {
        if (item.name() == requested) {
            return item.value().toString();
        }
    }
    return {};
}

Diagnostic diagnostic(
    DiagnosticSeverity severity,
    std::string code,
    std::string message,
    const std::filesystem::path& context)
{
    return {severity, std::move(code), std::move(message), context.string()};
}

void warningOnce(
    std::vector<Diagnostic>& diagnostics,
    std::string code,
    std::string message,
    const std::filesystem::path& context)
{
    const auto alreadyPresent = std::ranges::any_of(diagnostics, [&](const Diagnostic& item) {
        return item.code == code;
    });
    if (!alreadyPresent) {
        diagnostics.push_back(diagnostic(
            DiagnosticSeverity::warning, std::move(code), std::move(message), context));
    }
}

std::string zipOpenError(int code)
{
    zip_error_t error;
    zip_error_init_with_code(&error, code);
    const std::string message = zip_error_strerror(&error);
    zip_error_fini(&error);
    return message;
}

class ZipArchive final {
public:
    explicit ZipArchive(zip_t* archive)
        : archive_(archive)
    {
    }

    ~ZipArchive()
    {
        if (archive_) {
            zip_discard(archive_);
        }
    }

    ZipArchive(const ZipArchive&) = delete;
    ZipArchive& operator=(const ZipArchive&) = delete;

    [[nodiscard]] zip_t* get() const noexcept
    {
        return archive_;
    }

    [[nodiscard]] zip_t* release() noexcept
    {
        return std::exchange(archive_, nullptr);
    }

private:
    zip_t* archive_{nullptr};
};

std::optional<ByteBuffer> readPart(
    zip_t* archive,
    std::string_view name,
    std::uint64_t maximumBytes,
    const std::filesystem::path& source,
    std::vector<Diagnostic>& diagnostics,
    bool required = true)
{
    zip_stat_t status;
    zip_stat_init(&status);
    const std::string partName{name};
    if (zip_stat(archive, partName.c_str(), ZIP_FL_ENC_UTF_8, &status) != 0) {
        if (required) {
            diagnostics.push_back(diagnostic(
                DiagnosticSeverity::error,
                "docx.missing_part",
                "The DOCX package is missing required part " + partName + ".",
                source));
        }
        return std::nullopt;
    }
    if ((status.valid & ZIP_STAT_SIZE) == 0U || status.size > maximumBytes
        || status.size > static_cast<zip_uint64_t>(std::numeric_limits<std::ptrdiff_t>::max())) {
        diagnostics.push_back(diagnostic(
            DiagnosticSeverity::error,
            "docx.part_too_large",
            "The DOCX XML part exceeds the configured read limit: " + partName + ".",
            source));
        return std::nullopt;
    }

    zip_file_t* file = zip_fopen(archive, partName.c_str(), ZIP_FL_ENC_UTF_8);
    if (!file) {
        diagnostics.push_back(diagnostic(
            DiagnosticSeverity::error,
            "docx.part_open_failed",
            "The DOCX part could not be opened: " + partName + ".",
            source));
        return std::nullopt;
    }

    ByteBuffer bytes(static_cast<std::ptrdiff_t>(status.size), '\0');
    zip_uint64_t offset = 0;
    while (offset < status.size) {
        const auto remaining = status.size - offset;
        const auto count = zip_fread(
            file, bytes.data() + static_cast<std::ptrdiff_t>(offset), remaining);
        if (count < 0) {
            const std::string message = zip_file_strerror(file);
            zip_fclose(file);
            diagnostics.push_back(diagnostic(
                DiagnosticSeverity::error,
                "docx.part_read_failed",
                "The DOCX part could not be read: " + partName + ": " + message,
                source));
            return std::nullopt;
        }
        if (count == 0) {
            break;
        }
        offset += static_cast<zip_uint64_t>(count);
    }
    zip_fclose(file);
    if (offset != status.size) {
        diagnostics.push_back(diagnostic(
            DiagnosticSeverity::error,
            "docx.part_truncated",
            "The DOCX part ended before its declared size: " + partName + ".",
            source));
        return std::nullopt;
    }
    return bytes;
}

struct PackageRelationships {
    std::string mainDocumentPart;
    std::string corePropertiesPart;
    std::string customPropertiesPart;
};

std::optional<std::string> normalizedPackageTarget(const Text& target)
{
    auto value = target;
    value.replace('\\', '/');
    while (value.startsWith('/')) {
        value.remove(0, 1);
    }
    const auto path = std::filesystem::path(value.toStdString()).lexically_normal();
    const Text cleaned(path.generic_string());
    if (cleaned.isEmpty() || cleaned == Text(".")
        || cleaned == Text("..") || cleaned.startsWith(Text("../"))
        || path.is_absolute() || path.has_root_name()) {
        return std::nullopt;
    }
    return toUtf8(cleaned);
}

PackageRelationships parsePackageRelationships(
    const ByteBuffer& bytes,
    const std::filesystem::path& source,
    std::vector<Diagnostic>& diagnostics)
{
    PackageRelationships result;
    XmlReader xml(bytes);
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement() || xml.name() != Text("Relationship")) {
            continue;
        }
        const auto type = attribute(xml, "Type");
        const auto targetMode = attribute(xml, "TargetMode");
        if (targetMode.toLower() == Text("External").toLower()) {
            continue;
        }
        const auto target = normalizedPackageTarget(attribute(xml, "Target"));
        if (!target) {
            diagnostics.push_back(diagnostic(
                DiagnosticSeverity::error,
                "docx.invalid_relationship_target",
                "The DOCX package contains an unsafe relationship target.",
                source));
            continue;
        }
        if (type.endsWith(Text("/officeDocument"))) {
            result.mainDocumentPart = *target;
        } else if (type.endsWith(Text("/metadata/core-properties"))) {
            result.corePropertiesPart = *target;
        } else if (type.endsWith(Text("/custom-properties"))) {
            if (!result.customPropertiesPart.empty()) {
                diagnostics.push_back(diagnostic(DiagnosticSeverity::error, "authorship.duplicate", "Duplicate custom-properties relationship.", source));
            }
            result.customPropertiesPart = *target;
        }
    }
    if (xml.hasError()) {
        diagnostics.push_back(diagnostic(
            DiagnosticSeverity::error,
            "docx.relationships_xml_error",
            "The DOCX package relationships are malformed: " + toUtf8(xml.errorString()),
            source));
    }
    if (result.mainDocumentPart.empty()) {
        diagnostics.push_back(diagnostic(
            DiagnosticSeverity::error,
            "docx.main_relationship_missing",
            "The DOCX package has no main Word document relationship.",
            source));
    }
    return result;
}

const Text customNamespace = Text("http://schemas.openxmlformats.org/officeDocument/2006/custom-properties");
const Text variantNamespace = Text("http://schemas.openxmlformats.org/officeDocument/2006/docPropsVTypes");
void parseCustomProperties(const ByteBuffer &bytes, WordDocument &document,
                          const std::filesystem::path &source, std::vector<Diagnostic> &diagnostics) {
    XmlReader xml(bytes);
    bool sawAuthor = false;
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement() || xml.namespaceUri() != customNamespace || xml.name() != Text("property")) continue;
        if (xml.attributes().value(Text("name")) != Text::fromLatin1(iiFileProvider::native::Authorship::MetadataKey)) {
            xml.skipCurrentElement(); continue;
        }
        if (sawAuthor || !xml.readNextStartElement() || xml.namespaceUri() != variantNamespace || xml.name() != Text("lpwstr")) {
            xml.raiseError(Text("Invalid or duplicate authorship property")); break;
        }
        sawAuthor = true;
        document.metadata()[iiFileProvider::native::Authorship::MetadataKey] = toUtf8(xml.readElementText());
        if (xml.readNextStartElement()) { xml.raiseError(Text("Extra authorship property values")); break; }
    }
    if (xml.hasError()) diagnostics.push_back(diagnostic(DiagnosticSeverity::error, "authorship.invalid_xml",
        "Invalid DOCX authorship properties.", source));
}

void parseCoreProperties(
    const ByteBuffer& bytes,
    WordDocument& document,
    const std::filesystem::path& source,
    std::vector<Diagnostic>& diagnostics)
{
    static const std::map<Text, std::string> names{
        {Text("title"), "Title"},
        {Text("creator"), "Author"},
        {Text("subject"), "Subject"},
        {Text("description"), "Description"},
        {Text("keywords"), "Keywords"},
        {Text("lastModifiedBy"), "LastModifiedBy"},
        {Text("created"), "Created"},
        {Text("modified"), "Modified"},
    };

    XmlReader xml(bytes);
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement()) {
            continue;
        }
        const auto found = names.find(xml.name().toString());
        if (found != names.end()) {
            document.metadata()[found->second] = toUtf8(xml.readElementText());
        }
    }
    if (xml.hasError()) {
        diagnostics.push_back(diagnostic(
            DiagnosticSeverity::warning,
            "docx.core_properties_xml_error",
            "DOCX core properties could not be read completely: " + toUtf8(xml.errorString()),
            source));
    }
}

bool onOffValue(const XmlReader& xml, bool defaultValue = true)
{
    const auto value = attribute(xml, "val").toLower();
    if (value.isEmpty()) {
        return defaultValue;
    }
    return value != Text("0") && value != Text("false")
        && value != Text("off") && value != Text("none");
}

void parseRunProperties(XmlReader& xml, WordRunProperties& properties)
{
    while (xml.readNextStartElement()) {
        const auto name = xml.name();
        if (name == Text("b")) {
            properties.bold = onOffValue(xml);
            xml.skipCurrentElement();
        } else if (name == Text("i")) {
            properties.italic = onOffValue(xml);
            xml.skipCurrentElement();
        } else if (name == Text("u")) {
            properties.underline = onOffValue(xml);
            xml.skipCurrentElement();
        } else if (name == Text("rFonts")) {
            auto font = attribute(xml, "ascii");
            if (font.isEmpty()) {
                font = attribute(xml, "hAnsi");
            }
            properties.fontFamily = toUtf8(font);
            properties.eastAsiaFontFamily = toUtf8(attribute(xml, "eastAsia"));
            xml.skipCurrentElement();
        } else if (name == Text("sz")) {
            bool valid = false;
            const auto halfPoints = attribute(xml, "val").toInt(&valid);
            if (valid && halfPoints >= 0) {
                properties.fontSizePoints = static_cast<double>(halfPoints) / 2.0;
            }
            xml.skipCurrentElement();
        } else if (name == Text("color")) {
            const auto value = attribute(xml, "val");
            if (value.toLower() != Text("auto").toLower()) {
                properties.color = toUtf8(value.toUpper());
            }
            xml.skipCurrentElement();
        } else {
            xml.skipCurrentElement();
        }
    }
}

WordRun parseRun(
    XmlReader& xml,
    const std::filesystem::path& source,
    std::vector<Diagnostic>& diagnostics)
{
    WordRun run;
    while (xml.readNextStartElement()) {
        const auto name = xml.name();
        if (name == Text("rPr")) {
            parseRunProperties(xml, run.properties);
        } else if (name == Text("t") || name == Text("delText")) {
            run.text += toUtf8(xml.readElementText());
        } else if (name == Text("tab")) {
            run.text.push_back('\t');
            xml.skipCurrentElement();
        } else if (name == Text("br") || name == Text("cr")) {
            run.text.push_back('\n');
            xml.skipCurrentElement();
        } else if (name == Text("drawing") || name == Text("pict")
                   || name == Text("object")) {
            warningOnce(
                diagnostics,
                "docx.non_text_run_content_omitted",
                "Images, drawings, and embedded objects are not first-class Word model items yet.",
                source);
            xml.skipCurrentElement();
        } else {
            xml.skipCurrentElement();
        }
    }
    return run;
}

void parseParagraphProperties(XmlReader& xml, WordParagraphProperties& properties)
{
    while (xml.readNextStartElement()) {
        const auto name = xml.name();
        if (name == Text("pStyle")) {
            properties.styleId = toUtf8(attribute(xml, "val"));
            xml.skipCurrentElement();
        } else if (name == Text("jc")) {
            const auto value = attribute(xml, "val").toLower();
            if (value == Text("left") || value == Text("start")) {
                properties.alignment = WordParagraphAlignment::left;
            } else if (value == Text("center")) {
                properties.alignment = WordParagraphAlignment::center;
            } else if (value == Text("right") || value == Text("end")) {
                properties.alignment = WordParagraphAlignment::right;
            } else if (value == Text("both") || value == Text("distribute")) {
                properties.alignment = WordParagraphAlignment::justified;
            }
            xml.skipCurrentElement();
        } else if (name == Text("numPr")) {
            while (xml.readNextStartElement()) {
                if (xml.name() == Text("numId")) {
                    bool valid = false;
                    const auto value = attribute(xml, "val").toInt(&valid);
                    if (valid) {
                        properties.numberingId = value;
                    }
                    xml.skipCurrentElement();
                } else if (xml.name() == Text("ilvl")) {
                    bool valid = false;
                    const auto value = attribute(xml, "val").toInt(&valid);
                    if (valid) {
                        properties.numberingLevel = std::clamp(value, 0, 8);
                    }
                    xml.skipCurrentElement();
                } else {
                    xml.skipCurrentElement();
                }
            }
        } else {
            xml.skipCurrentElement();
        }
    }
}

void parseInlineContainer(
    XmlReader& xml,
    WordParagraph& paragraph,
    const std::filesystem::path& source,
    std::vector<Diagnostic>& diagnostics)
{
    while (xml.readNextStartElement()) {
        const auto name = xml.name();
        if (name == Text("r")) {
            paragraph.runs.push_back(parseRun(xml, source, diagnostics));
        } else if (name == Text("hyperlink") || name == Text("ins")
                   || name == Text("smartTag") || name == Text("sdt")
                   || name == Text("sdtContent") || name == Text("fldSimple")) {
            if (name == Text("hyperlink")) {
                warningOnce(
                    diagnostics,
                    "docx.hyperlink_flattened",
                    "Hyperlink text is readable, but its relationship is not first-class yet.",
                    source);
            }
            parseInlineContainer(xml, paragraph, source, diagnostics);
        } else if (name == Text("del") || name == Text("moveFrom")) {
            warningOnce(
                diagnostics,
                "docx.tracked_deletion_omitted",
                "Tracked deletions are excluded from the current document text.",
                source);
            xml.skipCurrentElement();
        } else {
            xml.skipCurrentElement();
        }
    }
}

WordParagraph parseParagraph(
    XmlReader& xml,
    const std::filesystem::path& source,
    std::vector<Diagnostic>& diagnostics)
{
    WordParagraph paragraph;
    while (xml.readNextStartElement()) {
        if (xml.name() == Text("pPr")) {
            parseParagraphProperties(xml, paragraph.properties);
        } else if (xml.name() == Text("r")) {
            paragraph.runs.push_back(parseRun(xml, source, diagnostics));
        } else if (xml.name() == Text("hyperlink") || xml.name() == Text("ins")
                   || xml.name() == Text("smartTag") || xml.name() == Text("sdt")
                   || xml.name() == Text("sdtContent")
                   || xml.name() == Text("fldSimple")) {
            if (xml.name() == Text("hyperlink")) {
                warningOnce(
                    diagnostics,
                    "docx.hyperlink_flattened",
                    "Hyperlink text is readable, but its relationship is not first-class yet.",
                    source);
            }
            parseInlineContainer(xml, paragraph, source, diagnostics);
        } else if (xml.name() == Text("del") || xml.name() == Text("moveFrom")) {
            warningOnce(
                diagnostics,
                "docx.tracked_deletion_omitted",
                "Tracked deletions are excluded from the current document text.",
                source);
            xml.skipCurrentElement();
        } else {
            xml.skipCurrentElement();
        }
    }
    return paragraph;
}

WordTableCell parseTableCell(
    XmlReader& xml,
    const std::filesystem::path& source,
    std::vector<Diagnostic>& diagnostics)
{
    WordTableCell cell;
    while (xml.readNextStartElement()) {
        if (xml.name() == Text("p")) {
            cell.paragraphs.push_back(parseParagraph(xml, source, diagnostics));
        } else if (xml.name() == Text("tbl")) {
            warningOnce(
                diagnostics,
                "docx.nested_table_omitted",
                "Nested tables are not first-class Word model items yet.",
                source);
            xml.skipCurrentElement();
        } else {
            xml.skipCurrentElement();
        }
    }
    if (cell.paragraphs.empty()) {
        cell.paragraphs.emplace_back();
    }
    return cell;
}

WordTableRow parseTableRow(
    XmlReader& xml,
    const std::filesystem::path& source,
    std::vector<Diagnostic>& diagnostics)
{
    WordTableRow row;
    while (xml.readNextStartElement()) {
        if (xml.name() == Text("tc")) {
            row.cells.push_back(parseTableCell(xml, source, diagnostics));
        } else {
            xml.skipCurrentElement();
        }
    }
    return row;
}

WordTable parseTable(
    XmlReader& xml,
    const std::filesystem::path& source,
    std::vector<Diagnostic>& diagnostics)
{
    WordTable table;
    while (xml.readNextStartElement()) {
        if (xml.name() == Text("tr")) {
            table.rows.push_back(parseTableRow(xml, source, diagnostics));
        } else {
            xml.skipCurrentElement();
        }
    }
    return table;
}

void parseSectionProperties(XmlReader& xml, WordSectionProperties& section)
{
    while (xml.readNextStartElement()) {
        if (xml.name() == Text("pgSz")) {
            bool widthValid = false;
            bool heightValid = false;
            const auto width = attribute(xml, "w").toInt(&widthValid);
            const auto height = attribute(xml, "h").toInt(&heightValid);
            if (widthValid && width > 0) {
                section.pageWidthTwips = width;
            }
            if (heightValid && height > 0) {
                section.pageHeightTwips = height;
            }
            xml.skipCurrentElement();
        } else if (xml.name() == Text("pgMar")) {
            const auto assign = [&](const char* name, int& target) {
                bool valid = false;
                const auto value = attribute(xml, name).toInt(&valid);
                if (valid && value >= 0) {
                    target = value;
                }
            };
            assign("top", section.marginTopTwips);
            assign("right", section.marginRightTwips);
            assign("bottom", section.marginBottomTwips);
            assign("left", section.marginLeftTwips);
            xml.skipCurrentElement();
        } else {
            xml.skipCurrentElement();
        }
    }
}

void parseDocumentXml(
    const ByteBuffer& bytes,
    WordDocument& document,
    const std::filesystem::path& source,
    std::vector<Diagnostic>& diagnostics)
{
    XmlReader xml(bytes);
    bool foundBody = false;
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement() || xml.name() != Text("body")) {
            continue;
        }
        foundBody = true;
        while (xml.readNextStartElement()) {
            if (xml.name() == Text("p")) {
                document.blocks().emplace_back(parseParagraph(xml, source, diagnostics));
            } else if (xml.name() == Text("tbl")) {
                document.blocks().emplace_back(parseTable(xml, source, diagnostics));
            } else if (xml.name() == Text("sectPr")) {
                parseSectionProperties(xml, document.section());
            } else {
                warningOnce(
                    diagnostics,
                    "docx.unsupported_body_block_omitted",
                    "A Word body block is not first-class and was omitted during reading.",
                    source);
                xml.skipCurrentElement();
            }
        }
        break;
    }
    if (xml.hasError()) {
        diagnostics.push_back(diagnostic(
            DiagnosticSeverity::error,
            "docx.document_xml_error",
            "The WordprocessingML document is malformed: " + toUtf8(xml.errorString()),
            source));
    } else if (!foundBody) {
        diagnostics.push_back(diagnostic(
            DiagnosticSeverity::error,
            "docx.body_missing",
            "The WordprocessingML document has no body element.",
            source));
    }
}

ByteBuffer xmlDocument(const std::function<void(XmlWriter&)>& body)
{
    ByteBuffer bytes;
    XmlWriter xml(bytes);
    xml.setAutoFormatting(false);
    xml.writeStartDocument(Text("1.0"));
    body(xml);
    xml.writeEndDocument();
    return bytes;
}

void writeValElement(XmlWriter& xml, const Text& name, const Text& value)
{
    xml.writeStartElement(wordNamespace(), name);
    xml.writeAttribute(wordNamespace(), Text("val"), value);
    xml.writeEndElement();
}

void writeRunProperties(XmlWriter& xml, const WordRunProperties& properties)
{
    const bool hasProperties = properties.bold || properties.italic || properties.underline
        || !properties.fontFamily.empty() || !properties.eastAsiaFontFamily.empty()
        || properties.fontSizePoints > 0.0 || !properties.color.empty();
    if (!hasProperties) {
        return;
    }

    xml.writeStartElement(wordNamespace(), Text("rPr"));
    if (properties.bold) {
        xml.writeEmptyElement(wordNamespace(), Text("b"));
    }
    if (properties.italic) {
        xml.writeEmptyElement(wordNamespace(), Text("i"));
    }
    if (properties.underline) {
        writeValElement(xml, Text("u"), Text("single"));
    }
    if (!properties.fontFamily.empty()) {
        xml.writeStartElement(wordNamespace(), Text("rFonts"));
        const auto font = Text::fromUtf8(properties.fontFamily);
        xml.writeAttribute(wordNamespace(), Text("ascii"), font);
        xml.writeAttribute(wordNamespace(), Text("hAnsi"), font);
        if (!properties.eastAsiaFontFamily.empty()) {
            xml.writeAttribute(wordNamespace(), Text("eastAsia"),
                               Text::fromUtf8(properties.eastAsiaFontFamily));
        }
        xml.writeEndElement();
    } else if (!properties.eastAsiaFontFamily.empty()) {
        xml.writeStartElement(wordNamespace(), Text("rFonts"));
        xml.writeAttribute(wordNamespace(), Text("eastAsia"),
                           Text::fromUtf8(properties.eastAsiaFontFamily));
        xml.writeEndElement();
    }
    if (properties.fontSizePoints > 0.0) {
        const auto halfPoints = static_cast<int>(std::lround(properties.fontSizePoints * 2.0));
        writeValElement(xml, Text("sz"), Text::number(halfPoints));
        writeValElement(xml, Text("szCs"), Text::number(halfPoints));
    }
    if (!properties.color.empty()) {
        writeValElement(xml, Text("color"), Text::fromUtf8(properties.color));
    }
    xml.writeEndElement();
}

void writeRunText(XmlWriter& xml, const std::string& text)
{
    const auto value = Text::fromUtf8(text);
    Text chunk;
    auto flush = [&]() {
        if (chunk.isEmpty()) {
            return;
        }
        xml.writeStartElement(wordNamespace(), Text("t"));
        xml.writeAttribute(
            Text("http://www.w3.org/XML/1998/namespace"),
            Text("space"), Text("preserve"));
        xml.writeCharacters(chunk);
        xml.writeEndElement();
        chunk.clear();
    };

    for (std::ptrdiff_t index = 0; index < value.size(); ++index) {
        const auto character = value[index];
        if (character == '\t') {
            flush();
            xml.writeEmptyElement(wordNamespace(), Text("tab"));
        } else if (character == '\n' || character == '\r') {
            flush();
            xml.writeEmptyElement(wordNamespace(), Text("br"));
            if (character == '\r' && index + 1 < value.size()
                && value[index + 1] == '\n') {
                ++index;
            }
        } else {
            chunk += character;
        }
    }
    flush();
}

void writeParagraph(XmlWriter& xml, const WordParagraph& paragraph)
{
    xml.writeStartElement(wordNamespace(), Text("p"));
    const auto& properties = paragraph.properties;
    if (!properties.styleId.empty()
        || properties.alignment != WordParagraphAlignment::automatic
        || (properties.numberingId.has_value()
            && !properties.numberingContinuation)) {
        xml.writeStartElement(wordNamespace(), Text("pPr"));
        if (!properties.styleId.empty()) {
            writeValElement(
                xml, Text("pStyle"), Text::fromUtf8(properties.styleId));
        }
        Text alignment;
        switch (properties.alignment) {
        case WordParagraphAlignment::left:
            alignment = Text("left");
            break;
        case WordParagraphAlignment::center:
            alignment = Text("center");
            break;
        case WordParagraphAlignment::right:
            alignment = Text("right");
            break;
        case WordParagraphAlignment::justified:
            alignment = Text("both");
            break;
        case WordParagraphAlignment::automatic:
            break;
        }
        if (!alignment.isEmpty()) {
            writeValElement(xml, Text("jc"), alignment);
        }
        if (properties.numberingId && !properties.numberingContinuation) {
            xml.writeStartElement(wordNamespace(), Text("numPr"));
            writeValElement(
                xml, Text("ilvl"), Text::number(properties.numberingLevel));
            writeValElement(
                xml, Text("numId"), Text::number(*properties.numberingId));
            xml.writeEndElement();
        }
        xml.writeEndElement();
    }

    for (const auto& run : paragraph.runs) {
        xml.writeStartElement(wordNamespace(), Text("r"));
        writeRunProperties(xml, run.properties);
        writeRunText(xml, run.text);
        xml.writeEndElement();
    }
    xml.writeEndElement();
}

void writeTable(XmlWriter& xml, const WordTable& table, int usableWidthTwips)
{
    std::size_t columnCount = 1;
    for (const auto& row : table.rows) {
        columnCount = std::max(columnCount, row.cells.size());
    }
    const auto safeWidth = std::max(usableWidthTwips, 1440);
    const auto baseWidth = safeWidth / static_cast<int>(columnCount);
    const auto remainder = safeWidth % static_cast<int>(columnCount);
    std::vector<int> widths(columnCount, baseWidth);
    for (int index = 0; index < remainder; ++index) {
        ++widths[static_cast<std::size_t>(index)];
    }

    xml.writeStartElement(wordNamespace(), Text("tbl"));
    xml.writeStartElement(wordNamespace(), Text("tblPr"));
    xml.writeStartElement(wordNamespace(), Text("tblW"));
    xml.writeAttribute(wordNamespace(), Text("w"), Text::number(safeWidth));
    xml.writeAttribute(wordNamespace(), Text("type"), Text("dxa"));
    xml.writeEndElement();
    xml.writeStartElement(wordNamespace(), Text("tblInd"));
    xml.writeAttribute(wordNamespace(), Text("w"), Text("120"));
    xml.writeAttribute(wordNamespace(), Text("type"), Text("dxa"));
    xml.writeEndElement();
    xml.writeStartElement(wordNamespace(), Text("tblLayout"));
    xml.writeAttribute(wordNamespace(), Text("type"), Text("fixed"));
    xml.writeEndElement();
    xml.writeStartElement(wordNamespace(), Text("tblCellMar"));
    for (const auto* side : {"top", "left", "bottom", "right"}) {
        xml.writeStartElement(wordNamespace(), Text::fromLatin1(side));
        xml.writeAttribute(wordNamespace(), Text("w"), Text("120"));
        xml.writeAttribute(wordNamespace(), Text("type"), Text("dxa"));
        xml.writeEndElement();
    }
    xml.writeEndElement();
    xml.writeStartElement(wordNamespace(), Text("tblBorders"));
    for (const auto* side : {"top", "left", "bottom", "right", "insideH", "insideV"}) {
        xml.writeStartElement(wordNamespace(), Text::fromLatin1(side));
        xml.writeAttribute(wordNamespace(), Text("val"), Text("single"));
        xml.writeAttribute(wordNamespace(), Text("sz"), Text("4"));
        xml.writeAttribute(wordNamespace(), Text("color"), Text("B7C9D6"));
        xml.writeEndElement();
    }
    xml.writeEndElement();
    xml.writeEndElement();

    xml.writeStartElement(wordNamespace(), Text("tblGrid"));
    for (const auto width : widths) {
        xml.writeStartElement(wordNamespace(), Text("gridCol"));
        xml.writeAttribute(wordNamespace(), Text("w"), Text::number(width));
        xml.writeEndElement();
    }
    xml.writeEndElement();

    for (const auto& row : table.rows) {
        xml.writeStartElement(wordNamespace(), Text("tr"));
        for (std::size_t cellIndex = 0; cellIndex < row.cells.size(); ++cellIndex) {
            const auto& cell = row.cells[cellIndex];
            xml.writeStartElement(wordNamespace(), Text("tc"));
            xml.writeStartElement(wordNamespace(), Text("tcPr"));
            xml.writeStartElement(wordNamespace(), Text("tcW"));
            xml.writeAttribute(
                wordNamespace(), Text("w"), Text::number(widths[cellIndex]));
            xml.writeAttribute(wordNamespace(), Text("type"), Text("dxa"));
            xml.writeEndElement();
            writeValElement(xml, Text("vAlign"), Text("center"));
            xml.writeEndElement();
            if (cell.paragraphs.empty()) {
                writeParagraph(xml, {});
            } else {
                for (const auto& paragraph : cell.paragraphs) {
                    writeParagraph(xml, paragraph);
                }
            }
            xml.writeEndElement();
        }
        xml.writeEndElement();
    }
    xml.writeEndElement();
}

std::set<std::string> usedStyles(const WordDocument& document)
{
    std::set<std::string> styles;
    const auto collect = [&](const WordParagraph& paragraph) {
        if (!paragraph.properties.styleId.empty()) {
            styles.insert(paragraph.properties.styleId);
        }
    };
    for (const auto& block : document.blocks()) {
        if (const auto* paragraph = std::get_if<WordParagraph>(&block)) {
            collect(*paragraph);
        } else {
            for (const auto& row : std::get<WordTable>(block).rows) {
                for (const auto& cell : row.cells) {
                    for (const auto& paragraph : cell.paragraphs) {
                        collect(paragraph);
                    }
                }
            }
        }
    }
    return styles;
}

std::set<int> usedNumberingIds(const WordDocument& document)
{
    std::set<int> identifiers;
    const auto collect = [&](const WordParagraph& paragraph) {
        if (paragraph.properties.numberingId) {
            identifiers.insert(*paragraph.properties.numberingId);
        }
    };
    for (const auto& block : document.blocks()) {
        if (const auto* paragraph = std::get_if<WordParagraph>(&block)) {
            collect(*paragraph);
        } else {
            for (const auto& row : std::get<WordTable>(block).rows) {
                for (const auto& cell : row.cells) {
                    for (const auto& paragraph : cell.paragraphs) {
                        collect(paragraph);
                    }
                }
            }
        }
    }
    return identifiers;
}

ByteBuffer documentXml(const WordDocument& document)
{
    return xmlDocument([&](XmlWriter& xml) {
        xml.writeStartElement(Text("w:document"));
        xml.writeNamespace(wordNamespace(), Text("w"));
        xml.writeNamespace(
            Text("http://schemas.openxmlformats.org/officeDocument/2006/relationships"),
            Text("r"));
        xml.writeStartElement(wordNamespace(), Text("body"));

        if (document.blocks().empty()) {
            writeParagraph(xml, {});
        } else {
            const auto& section = document.section();
            const auto usableWidth = section.pageWidthTwips - section.marginLeftTwips
                - section.marginRightTwips;
            for (const auto& block : document.blocks()) {
                if (const auto* paragraph = std::get_if<WordParagraph>(&block)) {
                    writeParagraph(xml, *paragraph);
                } else {
                    writeTable(xml, std::get<WordTable>(block), usableWidth);
                }
            }
        }

        const auto& section = document.section();
        xml.writeStartElement(wordNamespace(), Text("sectPr"));
        xml.writeStartElement(wordNamespace(), Text("pgSz"));
        xml.writeAttribute(
            wordNamespace(), Text("w"), Text::number(section.pageWidthTwips));
        xml.writeAttribute(
            wordNamespace(), Text("h"), Text::number(section.pageHeightTwips));
        xml.writeEndElement();
        xml.writeStartElement(wordNamespace(), Text("pgMar"));
        xml.writeAttribute(
            wordNamespace(), Text("top"), Text::number(section.marginTopTwips));
        xml.writeAttribute(
            wordNamespace(), Text("right"), Text::number(section.marginRightTwips));
        xml.writeAttribute(
            wordNamespace(), Text("bottom"), Text::number(section.marginBottomTwips));
        xml.writeAttribute(
            wordNamespace(), Text("left"), Text::number(section.marginLeftTwips));
        xml.writeAttribute(wordNamespace(), Text("header"), Text("720"));
        xml.writeAttribute(wordNamespace(), Text("footer"), Text("720"));
        xml.writeAttribute(wordNamespace(), Text("gutter"), Text("0"));
        xml.writeEndElement();
        xml.writeEndElement();

        xml.writeEndElement();
        xml.writeEndElement();
    });
}

void writeStyle(
    XmlWriter& xml,
    const Text& id,
    const Text& name,
    int fontSizeHalfPoints,
    bool bold,
    int spaceBefore,
    int spaceAfter,
    bool isDefault = false)
{
    xml.writeStartElement(wordNamespace(), Text("style"));
    xml.writeAttribute(wordNamespace(), Text("type"), Text("paragraph"));
    xml.writeAttribute(wordNamespace(), Text("styleId"), id);
    if (isDefault) {
        xml.writeAttribute(wordNamespace(), Text("default"), Text("1"));
    }
    writeValElement(xml, Text("name"), name);
    if (!isDefault) {
        writeValElement(xml, Text("basedOn"), Text("Normal"));
        writeValElement(xml, Text("next"), Text("Normal"));
        xml.writeEmptyElement(wordNamespace(), Text("qFormat"));
    }
    xml.writeStartElement(wordNamespace(), Text("pPr"));
    xml.writeStartElement(wordNamespace(), Text("spacing"));
    xml.writeAttribute(wordNamespace(), Text("before"), Text::number(spaceBefore));
    xml.writeAttribute(wordNamespace(), Text("after"), Text::number(spaceAfter));
    xml.writeAttribute(wordNamespace(), Text("line"), Text("276"));
    xml.writeAttribute(wordNamespace(), Text("lineRule"), Text("auto"));
    xml.writeEndElement();
    xml.writeEndElement();
    xml.writeStartElement(wordNamespace(), Text("rPr"));
    if (bold) {
        xml.writeEmptyElement(wordNamespace(), Text("b"));
    }
    writeValElement(xml, Text("sz"), Text::number(fontSizeHalfPoints));
    writeValElement(xml, Text("szCs"), Text::number(fontSizeHalfPoints));
    xml.writeEndElement();
    xml.writeEndElement();
}

ByteBuffer stylesXml(const WordDocument& document)
{
    const std::set<std::string> builtIn{
        "Normal", "Title", "Subtitle", "Heading1", "Heading2", "Heading3"};
    return xmlDocument([&](XmlWriter& xml) {
        xml.writeStartElement(Text("w:styles"));
        xml.writeNamespace(wordNamespace(), Text("w"));
        xml.writeStartElement(wordNamespace(), Text("docDefaults"));
        xml.writeStartElement(wordNamespace(), Text("rPrDefault"));
        xml.writeStartElement(wordNamespace(), Text("rPr"));
        xml.writeStartElement(wordNamespace(), Text("rFonts"));
        for (const auto* attributeName : {"ascii", "hAnsi", "cs"}) {
            xml.writeAttribute(
                wordNamespace(), Text::fromLatin1(attributeName), Text("Arial"));
        }
        xml.writeEndElement();
        writeValElement(xml, Text("sz"), Text("22"));
        writeValElement(xml, Text("szCs"), Text("22"));
        xml.writeEndElement();
        xml.writeEndElement();
        xml.writeStartElement(wordNamespace(), Text("pPrDefault"));
        xml.writeStartElement(wordNamespace(), Text("pPr"));
        xml.writeStartElement(wordNamespace(), Text("spacing"));
        xml.writeAttribute(wordNamespace(), Text("after"), Text("160"));
        xml.writeAttribute(wordNamespace(), Text("line"), Text("276"));
        xml.writeAttribute(wordNamespace(), Text("lineRule"), Text("auto"));
        xml.writeEndElement();
        xml.writeEndElement();
        xml.writeEndElement();
        xml.writeEndElement();

        writeStyle(xml, Text("Normal"), Text("Normal"), 22, false, 0, 160, true);
        writeStyle(xml, Text("Title"), Text("Title"), 52, false, 0, 60);
        writeStyle(xml, Text("Subtitle"), Text("Subtitle"), 30, false, 0, 160);
        writeStyle(xml, Text("Heading1"), Text("heading 1"), 32, true, 240, 120);
        writeStyle(xml, Text("Heading2"), Text("heading 2"), 26, true, 200, 80);
        writeStyle(xml, Text("Heading3"), Text("heading 3"), 22, true, 160, 60);

        for (const auto& style : usedStyles(document)) {
            if (!builtIn.contains(style)) {
                writeStyle(
                    xml, Text::fromUtf8(style), Text::fromUtf8(style), 22, false, 0, 160);
            }
        }
        xml.writeEndElement();
    });
}

ByteBuffer numberingXml(const std::set<int>& identifiers)
{
    return xmlDocument([&](XmlWriter& xml) {
        xml.writeStartElement(Text("w:numbering"));
        xml.writeNamespace(wordNamespace(), Text("w"));
        for (const auto identifier : identifiers) {
            xml.writeStartElement(wordNamespace(), Text("abstractNum"));
            xml.writeAttribute(
                wordNamespace(), Text("abstractNumId"), Text::number(identifier));
            writeValElement(xml, Text("multiLevelType"), Text("multilevel"));
            for (int level = 0; level < 9; ++level) {
                xml.writeStartElement(wordNamespace(), Text("lvl"));
                xml.writeAttribute(wordNamespace(), Text("ilvl"), Text::number(level));
                writeValElement(xml, Text("start"), Text("1"));
                writeValElement(xml, Text("numFmt"), Text("decimal"));
                writeValElement(
                    xml, Text("lvlText"), Text("%") + Text::number(level + 1)
                        + Text("."));
                writeValElement(xml, Text("suff"), Text("tab"));
                xml.writeStartElement(wordNamespace(), Text("pPr"));
                xml.writeStartElement(wordNamespace(), Text("tabs"));
                xml.writeStartElement(wordNamespace(), Text("tab"));
                xml.writeAttribute(wordNamespace(), Text("val"), Text("num"));
                xml.writeAttribute(
                    wordNamespace(), Text("pos"), Text::number(720 + level * 360));
                xml.writeEndElement();
                xml.writeEndElement();
                xml.writeStartElement(wordNamespace(), Text("ind"));
                xml.writeAttribute(
                    wordNamespace(), Text("left"), Text::number(720 + level * 360));
                xml.writeAttribute(wordNamespace(), Text("hanging"), Text("360"));
                xml.writeEndElement();
                xml.writeEndElement();
                xml.writeEndElement();
            }
            xml.writeEndElement();
            xml.writeStartElement(wordNamespace(), Text("num"));
            xml.writeAttribute(wordNamespace(), Text("numId"), Text::number(identifier));
            writeValElement(
                xml, Text("abstractNumId"), Text::number(identifier));
            xml.writeEndElement();
        }
        xml.writeEndElement();
    });
}

ByteBuffer packageRelationshipsXml()
{
    return xmlDocument([](XmlWriter& xml) {
        xml.writeStartElement(Text("Relationships"));
        xml.writeDefaultNamespace(relationshipNamespace());
        const auto relationship = [&](const Text& id, const Text& type, const Text& target) {
            xml.writeStartElement(relationshipNamespace(), Text("Relationship"));
            xml.writeAttribute(Text("Id"), id);
            xml.writeAttribute(Text("Type"), type);
            xml.writeAttribute(Text("Target"), target);
            xml.writeEndElement();
        };
        relationship(
            Text("rId1"),
            Text("http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument"),
            Text("word/document.xml"));
        relationship(
            Text("rId2"),
            Text("http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties"),
            Text("docProps/core.xml"));
        relationship(
            Text("rId3"),
            Text("http://schemas.openxmlformats.org/officeDocument/2006/relationships/extended-properties"),
            Text("docProps/app.xml"));
        relationship(Text("rId4"),
            Text("http://schemas.openxmlformats.org/officeDocument/2006/relationships/custom-properties"),
            Text("docProps/custom.xml"));
        xml.writeEndElement();
    });
}

ByteBuffer documentRelationshipsXml(bool hasNumbering)
{
    return xmlDocument([&](XmlWriter& xml) {
        xml.writeStartElement(Text("Relationships"));
        xml.writeDefaultNamespace(relationshipNamespace());
        xml.writeStartElement(relationshipNamespace(), Text("Relationship"));
        xml.writeAttribute(Text("Id"), Text("rId1"));
        xml.writeAttribute(
            Text("Type"),
            Text("http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles"));
        xml.writeAttribute(Text("Target"), Text("styles.xml"));
        xml.writeEndElement();
        if (hasNumbering) {
            xml.writeStartElement(relationshipNamespace(), Text("Relationship"));
            xml.writeAttribute(Text("Id"), Text("rId2"));
            xml.writeAttribute(
                Text("Type"),
                Text("http://schemas.openxmlformats.org/officeDocument/2006/relationships/numbering"));
            xml.writeAttribute(Text("Target"), Text("numbering.xml"));
            xml.writeEndElement();
        }
        xml.writeEndElement();
    });
}

ByteBuffer contentTypesXml(bool hasNumbering)
{
    static const Text contentTypesNamespace =
        Text("http://schemas.openxmlformats.org/package/2006/content-types");
    return xmlDocument([&](XmlWriter& xml) {
        xml.writeStartElement(Text("Types"));
        xml.writeDefaultNamespace(contentTypesNamespace);
        const auto defaultType = [&](const Text& extension, const Text& contentType) {
            xml.writeStartElement(contentTypesNamespace, Text("Default"));
            xml.writeAttribute(Text("Extension"), extension);
            xml.writeAttribute(Text("ContentType"), contentType);
            xml.writeEndElement();
        };
        const auto overrideType = [&](const Text& partName, const Text& contentType) {
            xml.writeStartElement(contentTypesNamespace, Text("Override"));
            xml.writeAttribute(Text("PartName"), partName);
            xml.writeAttribute(Text("ContentType"), contentType);
            xml.writeEndElement();
        };
        defaultType(
            Text("rels"),
            Text("application/vnd.openxmlformats-package.relationships+xml"));
        defaultType(Text("xml"), Text("application/xml"));
        overrideType(
            Text("/word/document.xml"),
            Text("application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"));
        overrideType(
            Text("/word/styles.xml"),
            Text("application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml"));
        if (hasNumbering) {
            overrideType(
                Text("/word/numbering.xml"),
                Text("application/vnd.openxmlformats-officedocument.wordprocessingml.numbering+xml"));
        }
        overrideType(
            Text("/docProps/core.xml"),
            Text("application/vnd.openxmlformats-package.core-properties+xml"));
        overrideType(
            Text("/docProps/app.xml"),
            Text("application/vnd.openxmlformats-officedocument.extended-properties+xml"));
        overrideType(Text("/docProps/custom.xml"),
            Text("application/vnd.openxmlformats-officedocument.custom-properties+xml"));
        xml.writeEndElement();
    });
}

ByteBuffer customPropertiesXml(const WordDocument &document) {
    return xmlDocument([&](XmlWriter &xml) {
        xml.writeStartElement(Text("Properties"));
        xml.writeDefaultNamespace(customNamespace);
        xml.writeNamespace(variantNamespace, Text("vt"));
        if (!document.authorship().isEmpty()) {
            xml.writeStartElement(customNamespace, Text("property"));
            xml.writeAttribute(Text("fmtid"), Text("{D5CDD505-2E9C-101B-9397-08002B2CF9AE}"));
            xml.writeAttribute(Text("pid"), Text("2"));
            xml.writeAttribute(Text("name"), Text::fromLatin1(iiFileProvider::native::Authorship::MetadataKey));
            xml.writeTextElement(variantNamespace, Text("lpwstr"), Text::fromUtf8(document.authorship().dump()));
            xml.writeEndElement();
        }
        xml.writeEndElement();
    });
}

ByteBuffer corePropertiesXml(const WordDocument& document)
{
    static const Text cpNamespace =
        Text("http://schemas.openxmlformats.org/package/2006/metadata/core-properties");
    static const Text dcNamespace = Text("http://purl.org/dc/elements/1.1/");
    static const Text dctermsNamespace = Text("http://purl.org/dc/terms/");
    static const Text xsiNamespace = Text("http://www.w3.org/2001/XMLSchema-instance");

    return xmlDocument([&](XmlWriter& xml) {
        xml.writeStartElement(Text("cp:coreProperties"));
        xml.writeNamespace(cpNamespace, Text("cp"));
        xml.writeNamespace(dcNamespace, Text("dc"));
        xml.writeNamespace(dctermsNamespace, Text("dcterms"));
        xml.writeNamespace(xsiNamespace, Text("xsi"));
        const auto writeMetadata = [&](const char* key, const Text& ns, const Text& name) {
            const auto found = document.metadata().find(key);
            if (found != document.metadata().end() && !found->second.empty()) {
                xml.writeTextElement(ns, name, Text::fromUtf8(found->second));
            }
        };
        writeMetadata("Title", dcNamespace, Text("title"));
        writeMetadata("Author", dcNamespace, Text("creator"));
        writeMetadata("Subject", dcNamespace, Text("subject"));
        writeMetadata("Description", dcNamespace, Text("description"));
        writeMetadata("Keywords", cpNamespace, Text("keywords"));
        writeMetadata("LastModifiedBy", cpNamespace, Text("lastModifiedBy"));
        const auto writeDate = [&](const char* key, const Text& name) {
            const auto found = document.metadata().find(key);
            if (found != document.metadata().end() && !found->second.empty()) {
                xml.writeStartElement(dctermsNamespace, name);
                xml.writeAttribute(xsiNamespace, Text("type"), Text("dcterms:W3CDTF"));
                xml.writeCharacters(Text::fromUtf8(found->second));
                xml.writeEndElement();
            }
        };
        writeDate("Created", Text("created"));
        writeDate("Modified", Text("modified"));
        xml.writeEndElement();
    });
}

ByteBuffer appPropertiesXml()
{
    static const Text appNamespace =
        Text("http://schemas.openxmlformats.org/officeDocument/2006/extended-properties");
    return xmlDocument([](XmlWriter& xml) {
        xml.writeStartElement(Text("Properties"));
        xml.writeDefaultNamespace(appNamespace);
        xml.writeNamespace(
            Text("http://schemas.openxmlformats.org/officeDocument/2006/docPropsVTypes"),
            Text("vt"));
        xml.writeTextElement(appNamespace, Text("Application"), Text("iiGeneralDocument"));
        xml.writeTextElement(appNamespace, Text("AppVersion"), Text("0.1"));
        xml.writeEndElement();
    });
}

struct PackagePart {
    std::string name;
    ByteBuffer bytes;
};

bool isXmlPackagePart(const PackagePart& part)
{
    const std::string_view name{part.name};
    return name.ends_with(".xml") || name.ends_with(".rels");
}

bool isHexColor(const std::string& color)
{
    return color.size() == 6
        && std::ranges::all_of(color, [](unsigned char character) {
               return std::isxdigit(character) != 0;
           });
}

void validateParagraph(
    const WordParagraph& paragraph,
    const std::filesystem::path& destination,
    std::vector<Diagnostic>& diagnostics)
{
    if (paragraph.properties.numberingId && *paragraph.properties.numberingId < 0) {
        diagnostics.push_back(diagnostic(
            DiagnosticSeverity::error,
            "docx.invalid_numbering_id",
            "Word numbering identifiers must be non-negative.",
            destination));
    }
    if (paragraph.properties.numberingContinuation
        && !paragraph.properties.numberingId) {
        diagnostics.push_back(diagnostic(
            DiagnosticSeverity::error,
            "docx.invalid_numbering_continuation",
            "A list-item continuation requires a numbering identifier.",
            destination));
    }
    if (paragraph.properties.numberingLevel < 0 || paragraph.properties.numberingLevel > 8) {
        diagnostics.push_back(diagnostic(
            DiagnosticSeverity::error,
            "docx.invalid_numbering_level",
            "Word numbering levels must be between 0 and 8.",
            destination));
    }
    for (const auto& run : paragraph.runs) {
        if (!std::isfinite(run.properties.fontSizePoints)
            || run.properties.fontSizePoints < 0.0) {
            diagnostics.push_back(diagnostic(
                DiagnosticSeverity::error,
                "docx.invalid_font_size",
                "Word run font sizes must be finite and non-negative.",
                destination));
        }
        if (!run.properties.color.empty() && !isHexColor(run.properties.color)) {
            diagnostics.push_back(diagnostic(
                DiagnosticSeverity::error,
                "docx.invalid_color",
                "Word run colors must contain exactly six hexadecimal digits.",
                destination));
        }
    }
}

void validateNumberingContinuations(
    const std::vector<const WordParagraph*>& paragraphs,
    const std::filesystem::path& destination,
    std::vector<Diagnostic>& diagnostics)
{
    std::optional<int> activeIdentifier;
    std::array<bool, 9> hasItemAtLevel{};
    for (const auto* paragraph : paragraphs) {
        if (!paragraph->properties.numberingId) {
            activeIdentifier.reset();
            hasItemAtLevel.fill(false);
            continue;
        }
        if (activeIdentifier != paragraph->properties.numberingId) {
            activeIdentifier = paragraph->properties.numberingId;
            hasItemAtLevel.fill(false);
        }
        if (paragraph->properties.numberingLevel < 0
            || paragraph->properties.numberingLevel > 8) {
            continue;
        }
        const auto level = static_cast<std::size_t>(
            paragraph->properties.numberingLevel);
        if (paragraph->properties.numberingContinuation) {
            if (!hasItemAtLevel[level]) {
                diagnostics.push_back(diagnostic(
                    DiagnosticSeverity::error,
                    "docx.invalid_numbering_continuation",
                    "A list-item continuation has no preceding item at its level.",
                    destination));
            }
            continue;
        }
        hasItemAtLevel[level] = true;
        std::fill(
            hasItemAtLevel.begin() + static_cast<std::ptrdiff_t>(level + 1),
            hasItemAtLevel.end(), false);
    }
}

void validateDocument(
    const WordDocument& document,
    const std::filesystem::path& destination,
    std::vector<Diagnostic>& diagnostics)
{
    std::vector<const WordParagraph*> bodyParagraphs;
    for (const auto& block : document.blocks()) {
        if (const auto* paragraph = std::get_if<WordParagraph>(&block)) {
            validateParagraph(*paragraph, destination, diagnostics);
            bodyParagraphs.push_back(paragraph);
            continue;
        }
        validateNumberingContinuations(bodyParagraphs, destination, diagnostics);
        bodyParagraphs.clear();
        const auto& table = std::get<WordTable>(block);
        if (table.rows.empty()) {
            diagnostics.push_back(diagnostic(
                DiagnosticSeverity::error,
                "docx.empty_table",
                "Word tables must contain at least one row.",
                destination));
            continue;
        }
        for (const auto& row : table.rows) {
            if (row.cells.empty()) {
                diagnostics.push_back(diagnostic(
                    DiagnosticSeverity::error,
                    "docx.empty_table_row",
                    "Word table rows must contain at least one cell.",
                    destination));
            }
            for (const auto& cell : row.cells) {
                std::vector<const WordParagraph*> cellParagraphs;
                for (const auto& paragraph : cell.paragraphs) {
                    validateParagraph(paragraph, destination, diagnostics);
                    cellParagraphs.push_back(&paragraph);
                }
                validateNumberingContinuations(
                    cellParagraphs, destination, diagnostics);
            }
        }
    }
    validateNumberingContinuations(bodyParagraphs, destination, diagnostics);
}

} // namespace

WordReadResult readDocxPackage(
    const std::filesystem::path& source,
    const WordReadOptions& options)
{
    WordReadResult result;
    if (!std::filesystem::is_regular_file(source)) {
        result.diagnostics.push_back(diagnostic(
            DiagnosticSeverity::error,
            "docx.source_missing",
            "The DOCX source does not exist.",
            source));
        return result;
    }
    if (options.maximumXmlPartBytes == 0) {
        result.diagnostics.push_back(diagnostic(
            DiagnosticSeverity::error,
            "docx.invalid_part_limit",
            "The DOCX XML-part size limit must be positive.",
            source));
        return result;
    }

    int openError = 0;
    ZipArchive archive(openProviderZip(source, &openError));
    if (!archive.get()) {
        result.diagnostics.push_back(diagnostic(
            DiagnosticSeverity::error,
            "docx.open_failed",
            "The DOCX ZIP package could not be opened: " + zipOpenError(openError),
            source));
        return result;
    }

    const auto relationships = readPart(
        archive.get(), "_rels/.rels", options.maximumXmlPartBytes,
        source, result.diagnostics);
    if (!relationships) {
        return result;
    }
    const auto packageRelationships = parsePackageRelationships(
        *relationships, source, result.diagnostics);
    if (result.hasErrors()) {
        return result;
    }

    const auto documentPart = readPart(
        archive.get(), packageRelationships.mainDocumentPart,
        options.maximumXmlPartBytes, source, result.diagnostics);
    if (!documentPart) {
        return result;
    }
    parseDocumentXml(*documentPart, result.document, source, result.diagnostics);

    if (!packageRelationships.corePropertiesPart.empty()) {
        const auto coreProperties = readPart(
            archive.get(), packageRelationships.corePropertiesPart,
            options.maximumXmlPartBytes, source, result.diagnostics, false);
        if (coreProperties) {
            parseCoreProperties(*coreProperties, result.document, source, result.diagnostics);
        }
    }
    if (!packageRelationships.customPropertiesPart.empty()) {
        const auto custom = readPart(archive.get(), packageRelationships.customPropertiesPart,
            options.maximumXmlPartBytes, source, result.diagnostics);
        if (custom) parseCustomProperties(*custom, result.document, source, result.diagnostics);
    }
    try { result.document.restoreAuthorship(); }
    catch (const std::exception&) {
        result.diagnostics.push_back(diagnostic(DiagnosticSeverity::error, "authorship.invalid",
            "Invalid file authorship metadata.", source));
    }
    return result;
}

WordWriteResult writeDocxPackage(
    const WordDocument& document,
    const std::filesystem::path& destination,
    const WordWriteOptions& options)
{
    WordWriteResult result;
    if (destination.empty()) {
        result.diagnostics.push_back(diagnostic(
            DiagnosticSeverity::error,
            "docx.destination_missing",
            "A DOCX destination path is required.",
            destination));
        return result;
    }
    if (options.maximumXmlPartBytes == 0) {
        result.diagnostics.push_back(diagnostic(
            DiagnosticSeverity::error,
            "docx.invalid_part_limit",
            "The DOCX XML-part validation limit must be positive.",
            destination));
        return result;
    }
    validateDocument(document, destination, result.diagnostics);
    if (result.hasErrors()) {
        return result;
    }
    const auto& section = document.section();
    if (section.pageWidthTwips <= 0 || section.pageHeightTwips <= 0
        || section.marginTopTwips < 0 || section.marginRightTwips < 0
        || section.marginBottomTwips < 0 || section.marginLeftTwips < 0
        || section.marginLeftTwips + section.marginRightTwips >= section.pageWidthTwips
        || section.marginTopTwips + section.marginBottomTwips >= section.pageHeightTwips) {
        result.diagnostics.push_back(diagnostic(
            DiagnosticSeverity::error,
            "docx.invalid_section_geometry",
            "Word page dimensions and margins must describe a positive content area.",
            destination));
        return result;
    }

    std::error_code directoryError;
    if (!destination.parent_path().empty()) {
        try { iiFileProvider::native::File::createDirectories(iiFileProvider::native::File::pathString(destination.parent_path())); }
        catch (const iiFileProvider::native::FileError &) { directoryError = std::make_error_code(std::errc::io_error); }
    }
    if (directoryError) {
        result.diagnostics.push_back(diagnostic(
            DiagnosticSeverity::error,
            "docx.destination_directory_failed",
            "The DOCX destination directory could not be created: " + directoryError.message(),
            destination.parent_path()));
        return result;
    }

    const auto numberingIdentifiers = usedNumberingIds(document);
    const bool hasNumbering = !numberingIdentifiers.empty();
    std::vector<PackagePart> parts;
    parts.reserve(hasNumbering ? 8 : 7);
    parts.push_back({"[Content_Types].xml", contentTypesXml(hasNumbering)});
    parts.push_back({"_rels/.rels", packageRelationshipsXml()});
    parts.push_back({"docProps/core.xml", corePropertiesXml(document)});
    parts.push_back({"docProps/custom.xml", customPropertiesXml(document)});
    parts.push_back({"docProps/app.xml", appPropertiesXml()});
    parts.push_back({"word/document.xml", documentXml(document)});
    parts.push_back({"word/styles.xml", stylesXml(document)});
    parts.push_back({"word/_rels/document.xml.rels", documentRelationshipsXml(hasNumbering)});
    if (hasNumbering) {
        parts.push_back({"word/numbering.xml", numberingXml(numberingIdentifiers)});
    }

    for (const auto& part : parts) {
        if (isXmlPackagePart(part)
            && static_cast<std::uint64_t>(part.bytes.size())
                > options.maximumXmlPartBytes) {
            result.diagnostics.push_back(diagnostic(
                DiagnosticSeverity::error,
                "docx.part_too_large",
                "The generated DOCX XML part exceeds the configured write limit: "
                    + part.name + ".",
                destination));
        }
    }
    if (result.hasErrors()) {
        return result;
    }

    std::unique_ptr<iiFileProvider::native::StagedFile> temporary;
    try {
        temporary = std::make_unique<iiFileProvider::native::StagedFile>(iiFileProvider::native::File::pathString(destination));
    } catch (const iiFileProvider::native::FileError &error) {
        result.diagnostics.push_back(diagnostic(DiagnosticSeverity::error,
            "docx.temporary_file_failed", error.what(), destination));
        return result;
    }
    const auto temporaryPath = std::filesystem::path(temporary->path().u16string());

    int openError = 0;
    ZipArchive archive(zip_open(
        temporaryPath.string().c_str(), ZIP_CREATE | ZIP_TRUNCATE, &openError));
    if (!archive.get()) {
        result.diagnostics.push_back(diagnostic(
            DiagnosticSeverity::error,
            "docx.create_failed",
            "The DOCX ZIP package could not be created: " + zipOpenError(openError),
            destination));
        return result;
    }

    for (const auto& part : parts) {
        zip_source_t* source = zip_source_buffer(
            archive.get(), part.bytes.constData(), static_cast<zip_uint64_t>(part.bytes.size()), 0);
        if (!source) {
            result.diagnostics.push_back(diagnostic(
                DiagnosticSeverity::error,
                "docx.part_source_failed",
                "Unable to prepare DOCX part " + part.name + ": "
                    + std::string(zip_strerror(archive.get())),
                destination));
            return result;
        }
        const auto index = zip_file_add(
            archive.get(), part.name.c_str(), source, ZIP_FL_ENC_UTF_8 | ZIP_FL_OVERWRITE);
        if (index < 0) {
            zip_source_free(source);
            result.diagnostics.push_back(diagnostic(
                DiagnosticSeverity::error,
                "docx.part_add_failed",
                "Unable to add DOCX part " + part.name + ": "
                    + std::string(zip_strerror(archive.get())),
                destination));
            return result;
        }
        if (zip_set_file_compression(
                archive.get(), static_cast<zip_uint64_t>(index), ZIP_CM_DEFLATE, 6) != 0) {
            result.diagnostics.push_back(diagnostic(
                DiagnosticSeverity::error,
                "docx.part_compression_failed",
                "Unable to compress DOCX part " + part.name + ": "
                    + std::string(zip_strerror(archive.get())),
                destination));
            return result;
        }
    }

    zip_t* committedArchive = archive.release();
    if (zip_close(committedArchive) != 0) {
        const std::string message = zip_strerror(committedArchive);
        zip_discard(committedArchive);
        result.diagnostics.push_back(diagnostic(
            DiagnosticSeverity::error,
            "docx.commit_failed",
            "The DOCX package could not be committed: " + message,
            destination));
        return result;
    }

    WordReadOptions validationOptions;
    validationOptions.maximumXmlPartBytes = options.maximumXmlPartBytes;
    auto validation = readDocxPackage(temporaryPath, validationOptions);
    if (validation.hasErrors()) {
        result.diagnostics.push_back(diagnostic(
            DiagnosticSeverity::error,
            "docx.post_write_validation_failed",
            "The generated DOCX package could not be reopened before commit.",
            destination));
        result.diagnostics.insert(
            result.diagnostics.end(),
            std::make_move_iterator(validation.diagnostics.begin()),
            std::make_move_iterator(validation.diagnostics.end()));
        return result;
    }

    const auto commit = atomicReplacePreservingPermissions(
        temporaryPath, destination);
    if (!commit.succeeded) {
        result.diagnostics.push_back(diagnostic(
            DiagnosticSeverity::error,
            "docx." + commit.diagnosticSuffix,
            commit.message,
            destination));
        return result;
    }
    return result;
}

} // namespace ii::document::detail
