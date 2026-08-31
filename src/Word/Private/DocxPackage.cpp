#include "Word/Private/DocxPackage.h"

#include <QBuffer>
#include <QByteArray>
#include <QDir>
#include <QString>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <zip.h>

#include <algorithm>
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

const QString& wordNamespace()
{
    static const QString value =
        QStringLiteral("http://schemas.openxmlformats.org/wordprocessingml/2006/main");
    return value;
}

const QString& relationshipNamespace()
{
    static const QString value =
        QStringLiteral("http://schemas.openxmlformats.org/package/2006/relationships");
    return value;
}

std::string toUtf8(const QString& value)
{
    const auto bytes = value.toUtf8();
    return {bytes.constData(), static_cast<std::size_t>(bytes.size())};
}

QString attribute(const QXmlStreamReader& xml, const char* localName)
{
    const auto requested = QString::fromLatin1(localName);
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

std::optional<QByteArray> readPart(
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
        || status.size > static_cast<zip_uint64_t>(std::numeric_limits<qsizetype>::max())) {
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

    QByteArray bytes(static_cast<qsizetype>(status.size), Qt::Uninitialized);
    zip_uint64_t offset = 0;
    while (offset < status.size) {
        const auto remaining = status.size - offset;
        const auto count = zip_fread(
            file, bytes.data() + static_cast<qsizetype>(offset), remaining);
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
};

std::optional<std::string> normalizedPackageTarget(const QString& target)
{
    auto value = target;
    value.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (value.startsWith(QLatin1Char('/'))) {
        value.remove(0, 1);
    }
    const auto cleaned = QDir::cleanPath(value);
    if (cleaned.isEmpty() || cleaned == QStringLiteral(".")
        || cleaned == QStringLiteral("..") || cleaned.startsWith(QStringLiteral("../"))
        || QDir::isAbsolutePath(cleaned)) {
        return std::nullopt;
    }
    return toUtf8(cleaned);
}

PackageRelationships parsePackageRelationships(
    const QByteArray& bytes,
    const std::filesystem::path& source,
    std::vector<Diagnostic>& diagnostics)
{
    PackageRelationships result;
    QXmlStreamReader xml(bytes);
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement() || xml.name() != QStringLiteral("Relationship")) {
            continue;
        }
        const auto type = attribute(xml, "Type");
        const auto targetMode = attribute(xml, "TargetMode");
        if (targetMode.compare(QStringLiteral("External"), Qt::CaseInsensitive) == 0) {
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
        if (type.endsWith(QStringLiteral("/officeDocument"))) {
            result.mainDocumentPart = *target;
        } else if (type.endsWith(QStringLiteral("/metadata/core-properties"))) {
            result.corePropertiesPart = *target;
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

void parseCoreProperties(
    const QByteArray& bytes,
    WordDocument& document,
    const std::filesystem::path& source,
    std::vector<Diagnostic>& diagnostics)
{
    static const std::map<QString, std::string> names{
        {QStringLiteral("title"), "Title"},
        {QStringLiteral("creator"), "Author"},
        {QStringLiteral("subject"), "Subject"},
        {QStringLiteral("description"), "Description"},
        {QStringLiteral("keywords"), "Keywords"},
        {QStringLiteral("lastModifiedBy"), "LastModifiedBy"},
        {QStringLiteral("created"), "Created"},
        {QStringLiteral("modified"), "Modified"},
    };

    QXmlStreamReader xml(bytes);
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

bool onOffValue(const QXmlStreamReader& xml, bool defaultValue = true)
{
    const auto value = attribute(xml, "val").toLower();
    if (value.isEmpty()) {
        return defaultValue;
    }
    return value != QStringLiteral("0") && value != QStringLiteral("false")
        && value != QStringLiteral("off") && value != QStringLiteral("none");
}

void parseRunProperties(QXmlStreamReader& xml, WordRunProperties& properties)
{
    while (xml.readNextStartElement()) {
        const auto name = xml.name();
        if (name == QStringLiteral("b")) {
            properties.bold = onOffValue(xml);
            xml.skipCurrentElement();
        } else if (name == QStringLiteral("i")) {
            properties.italic = onOffValue(xml);
            xml.skipCurrentElement();
        } else if (name == QStringLiteral("u")) {
            properties.underline = onOffValue(xml);
            xml.skipCurrentElement();
        } else if (name == QStringLiteral("rFonts")) {
            auto font = attribute(xml, "ascii");
            if (font.isEmpty()) {
                font = attribute(xml, "hAnsi");
            }
            properties.fontFamily = toUtf8(font);
            properties.eastAsiaFontFamily = toUtf8(attribute(xml, "eastAsia"));
            xml.skipCurrentElement();
        } else if (name == QStringLiteral("sz")) {
            bool valid = false;
            const auto halfPoints = attribute(xml, "val").toInt(&valid);
            if (valid && halfPoints >= 0) {
                properties.fontSizePoints = static_cast<double>(halfPoints) / 2.0;
            }
            xml.skipCurrentElement();
        } else if (name == QStringLiteral("color")) {
            const auto value = attribute(xml, "val");
            if (value.compare(QStringLiteral("auto"), Qt::CaseInsensitive) != 0) {
                properties.color = toUtf8(value.toUpper());
            }
            xml.skipCurrentElement();
        } else {
            xml.skipCurrentElement();
        }
    }
}

WordRun parseRun(
    QXmlStreamReader& xml,
    const std::filesystem::path& source,
    std::vector<Diagnostic>& diagnostics)
{
    WordRun run;
    while (xml.readNextStartElement()) {
        const auto name = xml.name();
        if (name == QStringLiteral("rPr")) {
            parseRunProperties(xml, run.properties);
        } else if (name == QStringLiteral("t") || name == QStringLiteral("delText")) {
            run.text += toUtf8(xml.readElementText());
        } else if (name == QStringLiteral("tab")) {
            run.text.push_back('\t');
            xml.skipCurrentElement();
        } else if (name == QStringLiteral("br") || name == QStringLiteral("cr")) {
            run.text.push_back('\n');
            xml.skipCurrentElement();
        } else if (name == QStringLiteral("drawing") || name == QStringLiteral("pict")
                   || name == QStringLiteral("object")) {
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

void parseParagraphProperties(QXmlStreamReader& xml, WordParagraphProperties& properties)
{
    while (xml.readNextStartElement()) {
        const auto name = xml.name();
        if (name == QStringLiteral("pStyle")) {
            properties.styleId = toUtf8(attribute(xml, "val"));
            xml.skipCurrentElement();
        } else if (name == QStringLiteral("jc")) {
            const auto value = attribute(xml, "val").toLower();
            if (value == QStringLiteral("left") || value == QStringLiteral("start")) {
                properties.alignment = WordParagraphAlignment::left;
            } else if (value == QStringLiteral("center")) {
                properties.alignment = WordParagraphAlignment::center;
            } else if (value == QStringLiteral("right") || value == QStringLiteral("end")) {
                properties.alignment = WordParagraphAlignment::right;
            } else if (value == QStringLiteral("both") || value == QStringLiteral("distribute")) {
                properties.alignment = WordParagraphAlignment::justified;
            }
            xml.skipCurrentElement();
        } else if (name == QStringLiteral("numPr")) {
            while (xml.readNextStartElement()) {
                if (xml.name() == QStringLiteral("numId")) {
                    bool valid = false;
                    const auto value = attribute(xml, "val").toInt(&valid);
                    if (valid) {
                        properties.numberingId = value;
                    }
                    xml.skipCurrentElement();
                } else if (xml.name() == QStringLiteral("ilvl")) {
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
    QXmlStreamReader& xml,
    WordParagraph& paragraph,
    const std::filesystem::path& source,
    std::vector<Diagnostic>& diagnostics)
{
    while (xml.readNextStartElement()) {
        const auto name = xml.name();
        if (name == QStringLiteral("r")) {
            paragraph.runs.push_back(parseRun(xml, source, diagnostics));
        } else if (name == QStringLiteral("hyperlink") || name == QStringLiteral("ins")
                   || name == QStringLiteral("smartTag") || name == QStringLiteral("sdt")
                   || name == QStringLiteral("sdtContent") || name == QStringLiteral("fldSimple")) {
            if (name == QStringLiteral("hyperlink")) {
                warningOnce(
                    diagnostics,
                    "docx.hyperlink_flattened",
                    "Hyperlink text is readable, but its relationship is not first-class yet.",
                    source);
            }
            parseInlineContainer(xml, paragraph, source, diagnostics);
        } else if (name == QStringLiteral("del") || name == QStringLiteral("moveFrom")) {
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
    QXmlStreamReader& xml,
    const std::filesystem::path& source,
    std::vector<Diagnostic>& diagnostics)
{
    WordParagraph paragraph;
    while (xml.readNextStartElement()) {
        if (xml.name() == QStringLiteral("pPr")) {
            parseParagraphProperties(xml, paragraph.properties);
        } else if (xml.name() == QStringLiteral("r")) {
            paragraph.runs.push_back(parseRun(xml, source, diagnostics));
        } else if (xml.name() == QStringLiteral("hyperlink") || xml.name() == QStringLiteral("ins")
                   || xml.name() == QStringLiteral("smartTag") || xml.name() == QStringLiteral("sdt")
                   || xml.name() == QStringLiteral("sdtContent")
                   || xml.name() == QStringLiteral("fldSimple")) {
            if (xml.name() == QStringLiteral("hyperlink")) {
                warningOnce(
                    diagnostics,
                    "docx.hyperlink_flattened",
                    "Hyperlink text is readable, but its relationship is not first-class yet.",
                    source);
            }
            parseInlineContainer(xml, paragraph, source, diagnostics);
        } else if (xml.name() == QStringLiteral("del") || xml.name() == QStringLiteral("moveFrom")) {
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
    QXmlStreamReader& xml,
    const std::filesystem::path& source,
    std::vector<Diagnostic>& diagnostics)
{
    WordTableCell cell;
    while (xml.readNextStartElement()) {
        if (xml.name() == QStringLiteral("p")) {
            cell.paragraphs.push_back(parseParagraph(xml, source, diagnostics));
        } else if (xml.name() == QStringLiteral("tbl")) {
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
    QXmlStreamReader& xml,
    const std::filesystem::path& source,
    std::vector<Diagnostic>& diagnostics)
{
    WordTableRow row;
    while (xml.readNextStartElement()) {
        if (xml.name() == QStringLiteral("tc")) {
            row.cells.push_back(parseTableCell(xml, source, diagnostics));
        } else {
            xml.skipCurrentElement();
        }
    }
    return row;
}

WordTable parseTable(
    QXmlStreamReader& xml,
    const std::filesystem::path& source,
    std::vector<Diagnostic>& diagnostics)
{
    WordTable table;
    while (xml.readNextStartElement()) {
        if (xml.name() == QStringLiteral("tr")) {
            table.rows.push_back(parseTableRow(xml, source, diagnostics));
        } else {
            xml.skipCurrentElement();
        }
    }
    return table;
}

void parseSectionProperties(QXmlStreamReader& xml, WordSectionProperties& section)
{
    while (xml.readNextStartElement()) {
        if (xml.name() == QStringLiteral("pgSz")) {
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
        } else if (xml.name() == QStringLiteral("pgMar")) {
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
    const QByteArray& bytes,
    WordDocument& document,
    const std::filesystem::path& source,
    std::vector<Diagnostic>& diagnostics)
{
    QXmlStreamReader xml(bytes);
    bool foundBody = false;
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement() || xml.name() != QStringLiteral("body")) {
            continue;
        }
        foundBody = true;
        while (xml.readNextStartElement()) {
            if (xml.name() == QStringLiteral("p")) {
                document.appendParagraph(parseParagraph(xml, source, diagnostics));
            } else if (xml.name() == QStringLiteral("tbl")) {
                document.appendTable(parseTable(xml, source, diagnostics));
            } else if (xml.name() == QStringLiteral("sectPr")) {
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

QByteArray xmlDocument(const std::function<void(QXmlStreamWriter&)>& body)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    QXmlStreamWriter xml(&buffer);
    xml.setAutoFormatting(false);
    xml.writeStartDocument(QStringLiteral("1.0"));
    body(xml);
    xml.writeEndDocument();
    return bytes;
}

void writeValElement(QXmlStreamWriter& xml, const QString& name, const QString& value)
{
    xml.writeStartElement(wordNamespace(), name);
    xml.writeAttribute(wordNamespace(), QStringLiteral("val"), value);
    xml.writeEndElement();
}

void writeRunProperties(QXmlStreamWriter& xml, const WordRunProperties& properties)
{
    const bool hasProperties = properties.bold || properties.italic || properties.underline
        || !properties.fontFamily.empty() || !properties.eastAsiaFontFamily.empty()
        || properties.fontSizePoints > 0.0 || !properties.color.empty();
    if (!hasProperties) {
        return;
    }

    xml.writeStartElement(wordNamespace(), QStringLiteral("rPr"));
    if (properties.bold) {
        xml.writeEmptyElement(wordNamespace(), QStringLiteral("b"));
    }
    if (properties.italic) {
        xml.writeEmptyElement(wordNamespace(), QStringLiteral("i"));
    }
    if (properties.underline) {
        writeValElement(xml, QStringLiteral("u"), QStringLiteral("single"));
    }
    if (!properties.fontFamily.empty()) {
        xml.writeStartElement(wordNamespace(), QStringLiteral("rFonts"));
        const auto font = QString::fromUtf8(properties.fontFamily);
        xml.writeAttribute(wordNamespace(), QStringLiteral("ascii"), font);
        xml.writeAttribute(wordNamespace(), QStringLiteral("hAnsi"), font);
        if (!properties.eastAsiaFontFamily.empty()) {
            xml.writeAttribute(wordNamespace(), QStringLiteral("eastAsia"),
                               QString::fromUtf8(properties.eastAsiaFontFamily));
        }
        xml.writeEndElement();
    } else if (!properties.eastAsiaFontFamily.empty()) {
        xml.writeStartElement(wordNamespace(), QStringLiteral("rFonts"));
        xml.writeAttribute(wordNamespace(), QStringLiteral("eastAsia"),
                           QString::fromUtf8(properties.eastAsiaFontFamily));
        xml.writeEndElement();
    }
    if (properties.fontSizePoints > 0.0) {
        const auto halfPoints = static_cast<int>(std::lround(properties.fontSizePoints * 2.0));
        writeValElement(xml, QStringLiteral("sz"), QString::number(halfPoints));
        writeValElement(xml, QStringLiteral("szCs"), QString::number(halfPoints));
    }
    if (!properties.color.empty()) {
        writeValElement(xml, QStringLiteral("color"), QString::fromUtf8(properties.color));
    }
    xml.writeEndElement();
}

void writeRunText(QXmlStreamWriter& xml, const std::string& text)
{
    const auto value = QString::fromUtf8(text);
    QString chunk;
    auto flush = [&]() {
        if (chunk.isEmpty()) {
            return;
        }
        xml.writeStartElement(wordNamespace(), QStringLiteral("t"));
        xml.writeAttribute(
            QStringLiteral("http://www.w3.org/XML/1998/namespace"),
            QStringLiteral("space"), QStringLiteral("preserve"));
        xml.writeCharacters(chunk);
        xml.writeEndElement();
        chunk.clear();
    };

    for (qsizetype index = 0; index < value.size(); ++index) {
        const auto character = value[index];
        if (character == QLatin1Char('\t')) {
            flush();
            xml.writeEmptyElement(wordNamespace(), QStringLiteral("tab"));
        } else if (character == QLatin1Char('\n') || character == QLatin1Char('\r')) {
            flush();
            xml.writeEmptyElement(wordNamespace(), QStringLiteral("br"));
            if (character == QLatin1Char('\r') && index + 1 < value.size()
                && value[index + 1] == QLatin1Char('\n')) {
                ++index;
            }
        } else {
            chunk += character;
        }
    }
    flush();
}

void writeParagraph(QXmlStreamWriter& xml, const WordParagraph& paragraph)
{
    xml.writeStartElement(wordNamespace(), QStringLiteral("p"));
    const auto& properties = paragraph.properties;
    if (!properties.styleId.empty()
        || properties.alignment != WordParagraphAlignment::automatic
        || properties.numberingId.has_value()) {
        xml.writeStartElement(wordNamespace(), QStringLiteral("pPr"));
        if (!properties.styleId.empty()) {
            writeValElement(
                xml, QStringLiteral("pStyle"), QString::fromUtf8(properties.styleId));
        }
        QString alignment;
        switch (properties.alignment) {
        case WordParagraphAlignment::left:
            alignment = QStringLiteral("left");
            break;
        case WordParagraphAlignment::center:
            alignment = QStringLiteral("center");
            break;
        case WordParagraphAlignment::right:
            alignment = QStringLiteral("right");
            break;
        case WordParagraphAlignment::justified:
            alignment = QStringLiteral("both");
            break;
        case WordParagraphAlignment::automatic:
            break;
        }
        if (!alignment.isEmpty()) {
            writeValElement(xml, QStringLiteral("jc"), alignment);
        }
        if (properties.numberingId) {
            xml.writeStartElement(wordNamespace(), QStringLiteral("numPr"));
            writeValElement(
                xml, QStringLiteral("ilvl"), QString::number(properties.numberingLevel));
            writeValElement(
                xml, QStringLiteral("numId"), QString::number(*properties.numberingId));
            xml.writeEndElement();
        }
        xml.writeEndElement();
    }

    for (const auto& run : paragraph.runs) {
        xml.writeStartElement(wordNamespace(), QStringLiteral("r"));
        writeRunProperties(xml, run.properties);
        writeRunText(xml, run.text);
        xml.writeEndElement();
    }
    xml.writeEndElement();
}

void writeTable(QXmlStreamWriter& xml, const WordTable& table, int usableWidthTwips)
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

    xml.writeStartElement(wordNamespace(), QStringLiteral("tbl"));
    xml.writeStartElement(wordNamespace(), QStringLiteral("tblPr"));
    xml.writeStartElement(wordNamespace(), QStringLiteral("tblW"));
    xml.writeAttribute(wordNamespace(), QStringLiteral("w"), QString::number(safeWidth));
    xml.writeAttribute(wordNamespace(), QStringLiteral("type"), QStringLiteral("dxa"));
    xml.writeEndElement();
    xml.writeStartElement(wordNamespace(), QStringLiteral("tblInd"));
    xml.writeAttribute(wordNamespace(), QStringLiteral("w"), QStringLiteral("120"));
    xml.writeAttribute(wordNamespace(), QStringLiteral("type"), QStringLiteral("dxa"));
    xml.writeEndElement();
    xml.writeStartElement(wordNamespace(), QStringLiteral("tblLayout"));
    xml.writeAttribute(wordNamespace(), QStringLiteral("type"), QStringLiteral("fixed"));
    xml.writeEndElement();
    xml.writeStartElement(wordNamespace(), QStringLiteral("tblCellMar"));
    for (const auto* side : {"top", "left", "bottom", "right"}) {
        xml.writeStartElement(wordNamespace(), QString::fromLatin1(side));
        xml.writeAttribute(wordNamespace(), QStringLiteral("w"), QStringLiteral("120"));
        xml.writeAttribute(wordNamespace(), QStringLiteral("type"), QStringLiteral("dxa"));
        xml.writeEndElement();
    }
    xml.writeEndElement();
    xml.writeStartElement(wordNamespace(), QStringLiteral("tblBorders"));
    for (const auto* side : {"top", "left", "bottom", "right", "insideH", "insideV"}) {
        xml.writeStartElement(wordNamespace(), QString::fromLatin1(side));
        xml.writeAttribute(wordNamespace(), QStringLiteral("val"), QStringLiteral("single"));
        xml.writeAttribute(wordNamespace(), QStringLiteral("sz"), QStringLiteral("4"));
        xml.writeAttribute(wordNamespace(), QStringLiteral("color"), QStringLiteral("B7C9D6"));
        xml.writeEndElement();
    }
    xml.writeEndElement();
    xml.writeEndElement();

    xml.writeStartElement(wordNamespace(), QStringLiteral("tblGrid"));
    for (const auto width : widths) {
        xml.writeStartElement(wordNamespace(), QStringLiteral("gridCol"));
        xml.writeAttribute(wordNamespace(), QStringLiteral("w"), QString::number(width));
        xml.writeEndElement();
    }
    xml.writeEndElement();

    for (const auto& row : table.rows) {
        xml.writeStartElement(wordNamespace(), QStringLiteral("tr"));
        for (std::size_t cellIndex = 0; cellIndex < row.cells.size(); ++cellIndex) {
            const auto& cell = row.cells[cellIndex];
            xml.writeStartElement(wordNamespace(), QStringLiteral("tc"));
            xml.writeStartElement(wordNamespace(), QStringLiteral("tcPr"));
            xml.writeStartElement(wordNamespace(), QStringLiteral("tcW"));
            xml.writeAttribute(
                wordNamespace(), QStringLiteral("w"), QString::number(widths[cellIndex]));
            xml.writeAttribute(wordNamespace(), QStringLiteral("type"), QStringLiteral("dxa"));
            xml.writeEndElement();
            writeValElement(xml, QStringLiteral("vAlign"), QStringLiteral("center"));
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

QByteArray documentXml(const WordDocument& document)
{
    return xmlDocument([&](QXmlStreamWriter& xml) {
        xml.writeStartElement(QStringLiteral("w:document"));
        xml.writeNamespace(wordNamespace(), QStringLiteral("w"));
        xml.writeNamespace(
            QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships"),
            QStringLiteral("r"));
        xml.writeStartElement(wordNamespace(), QStringLiteral("body"));

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
        xml.writeStartElement(wordNamespace(), QStringLiteral("sectPr"));
        xml.writeStartElement(wordNamespace(), QStringLiteral("pgSz"));
        xml.writeAttribute(
            wordNamespace(), QStringLiteral("w"), QString::number(section.pageWidthTwips));
        xml.writeAttribute(
            wordNamespace(), QStringLiteral("h"), QString::number(section.pageHeightTwips));
        xml.writeEndElement();
        xml.writeStartElement(wordNamespace(), QStringLiteral("pgMar"));
        xml.writeAttribute(
            wordNamespace(), QStringLiteral("top"), QString::number(section.marginTopTwips));
        xml.writeAttribute(
            wordNamespace(), QStringLiteral("right"), QString::number(section.marginRightTwips));
        xml.writeAttribute(
            wordNamespace(), QStringLiteral("bottom"), QString::number(section.marginBottomTwips));
        xml.writeAttribute(
            wordNamespace(), QStringLiteral("left"), QString::number(section.marginLeftTwips));
        xml.writeAttribute(wordNamespace(), QStringLiteral("header"), QStringLiteral("720"));
        xml.writeAttribute(wordNamespace(), QStringLiteral("footer"), QStringLiteral("720"));
        xml.writeAttribute(wordNamespace(), QStringLiteral("gutter"), QStringLiteral("0"));
        xml.writeEndElement();
        xml.writeEndElement();

        xml.writeEndElement();
        xml.writeEndElement();
    });
}

void writeStyle(
    QXmlStreamWriter& xml,
    const QString& id,
    const QString& name,
    int fontSizeHalfPoints,
    bool bold,
    int spaceBefore,
    int spaceAfter,
    bool isDefault = false)
{
    xml.writeStartElement(wordNamespace(), QStringLiteral("style"));
    xml.writeAttribute(wordNamespace(), QStringLiteral("type"), QStringLiteral("paragraph"));
    xml.writeAttribute(wordNamespace(), QStringLiteral("styleId"), id);
    if (isDefault) {
        xml.writeAttribute(wordNamespace(), QStringLiteral("default"), QStringLiteral("1"));
    }
    writeValElement(xml, QStringLiteral("name"), name);
    if (!isDefault) {
        writeValElement(xml, QStringLiteral("basedOn"), QStringLiteral("Normal"));
        writeValElement(xml, QStringLiteral("next"), QStringLiteral("Normal"));
        xml.writeEmptyElement(wordNamespace(), QStringLiteral("qFormat"));
    }
    xml.writeStartElement(wordNamespace(), QStringLiteral("pPr"));
    xml.writeStartElement(wordNamespace(), QStringLiteral("spacing"));
    xml.writeAttribute(wordNamespace(), QStringLiteral("before"), QString::number(spaceBefore));
    xml.writeAttribute(wordNamespace(), QStringLiteral("after"), QString::number(spaceAfter));
    xml.writeAttribute(wordNamespace(), QStringLiteral("line"), QStringLiteral("276"));
    xml.writeAttribute(wordNamespace(), QStringLiteral("lineRule"), QStringLiteral("auto"));
    xml.writeEndElement();
    xml.writeEndElement();
    xml.writeStartElement(wordNamespace(), QStringLiteral("rPr"));
    if (bold) {
        xml.writeEmptyElement(wordNamespace(), QStringLiteral("b"));
    }
    writeValElement(xml, QStringLiteral("sz"), QString::number(fontSizeHalfPoints));
    writeValElement(xml, QStringLiteral("szCs"), QString::number(fontSizeHalfPoints));
    xml.writeEndElement();
    xml.writeEndElement();
}

QByteArray stylesXml(const WordDocument& document)
{
    const std::set<std::string> builtIn{
        "Normal", "Title", "Subtitle", "Heading1", "Heading2", "Heading3"};
    return xmlDocument([&](QXmlStreamWriter& xml) {
        xml.writeStartElement(QStringLiteral("w:styles"));
        xml.writeNamespace(wordNamespace(), QStringLiteral("w"));
        xml.writeStartElement(wordNamespace(), QStringLiteral("docDefaults"));
        xml.writeStartElement(wordNamespace(), QStringLiteral("rPrDefault"));
        xml.writeStartElement(wordNamespace(), QStringLiteral("rPr"));
        xml.writeStartElement(wordNamespace(), QStringLiteral("rFonts"));
        for (const auto* attributeName : {"ascii", "hAnsi", "cs"}) {
            xml.writeAttribute(
                wordNamespace(), QString::fromLatin1(attributeName), QStringLiteral("Arial"));
        }
        xml.writeEndElement();
        writeValElement(xml, QStringLiteral("sz"), QStringLiteral("22"));
        writeValElement(xml, QStringLiteral("szCs"), QStringLiteral("22"));
        xml.writeEndElement();
        xml.writeEndElement();
        xml.writeStartElement(wordNamespace(), QStringLiteral("pPrDefault"));
        xml.writeStartElement(wordNamespace(), QStringLiteral("pPr"));
        xml.writeStartElement(wordNamespace(), QStringLiteral("spacing"));
        xml.writeAttribute(wordNamespace(), QStringLiteral("after"), QStringLiteral("160"));
        xml.writeAttribute(wordNamespace(), QStringLiteral("line"), QStringLiteral("276"));
        xml.writeAttribute(wordNamespace(), QStringLiteral("lineRule"), QStringLiteral("auto"));
        xml.writeEndElement();
        xml.writeEndElement();
        xml.writeEndElement();
        xml.writeEndElement();

        writeStyle(xml, QStringLiteral("Normal"), QStringLiteral("Normal"), 22, false, 0, 160, true);
        writeStyle(xml, QStringLiteral("Title"), QStringLiteral("Title"), 52, false, 0, 60);
        writeStyle(xml, QStringLiteral("Subtitle"), QStringLiteral("Subtitle"), 30, false, 0, 160);
        writeStyle(xml, QStringLiteral("Heading1"), QStringLiteral("heading 1"), 32, true, 240, 120);
        writeStyle(xml, QStringLiteral("Heading2"), QStringLiteral("heading 2"), 26, true, 200, 80);
        writeStyle(xml, QStringLiteral("Heading3"), QStringLiteral("heading 3"), 22, true, 160, 60);

        for (const auto& style : usedStyles(document)) {
            if (!builtIn.contains(style)) {
                writeStyle(
                    xml, QString::fromUtf8(style), QString::fromUtf8(style), 22, false, 0, 160);
            }
        }
        xml.writeEndElement();
    });
}

QByteArray numberingXml(const std::set<int>& identifiers)
{
    return xmlDocument([&](QXmlStreamWriter& xml) {
        xml.writeStartElement(QStringLiteral("w:numbering"));
        xml.writeNamespace(wordNamespace(), QStringLiteral("w"));
        for (const auto identifier : identifiers) {
            xml.writeStartElement(wordNamespace(), QStringLiteral("abstractNum"));
            xml.writeAttribute(
                wordNamespace(), QStringLiteral("abstractNumId"), QString::number(identifier));
            writeValElement(xml, QStringLiteral("multiLevelType"), QStringLiteral("multilevel"));
            for (int level = 0; level < 9; ++level) {
                xml.writeStartElement(wordNamespace(), QStringLiteral("lvl"));
                xml.writeAttribute(wordNamespace(), QStringLiteral("ilvl"), QString::number(level));
                writeValElement(xml, QStringLiteral("start"), QStringLiteral("1"));
                writeValElement(xml, QStringLiteral("numFmt"), QStringLiteral("decimal"));
                writeValElement(
                    xml, QStringLiteral("lvlText"), QStringLiteral("%") + QString::number(level + 1)
                        + QStringLiteral("."));
                writeValElement(xml, QStringLiteral("suff"), QStringLiteral("tab"));
                xml.writeStartElement(wordNamespace(), QStringLiteral("pPr"));
                xml.writeStartElement(wordNamespace(), QStringLiteral("tabs"));
                xml.writeStartElement(wordNamespace(), QStringLiteral("tab"));
                xml.writeAttribute(wordNamespace(), QStringLiteral("val"), QStringLiteral("num"));
                xml.writeAttribute(
                    wordNamespace(), QStringLiteral("pos"), QString::number(720 + level * 360));
                xml.writeEndElement();
                xml.writeEndElement();
                xml.writeStartElement(wordNamespace(), QStringLiteral("ind"));
                xml.writeAttribute(
                    wordNamespace(), QStringLiteral("left"), QString::number(720 + level * 360));
                xml.writeAttribute(wordNamespace(), QStringLiteral("hanging"), QStringLiteral("360"));
                xml.writeEndElement();
                xml.writeEndElement();
                xml.writeEndElement();
            }
            xml.writeEndElement();
            xml.writeStartElement(wordNamespace(), QStringLiteral("num"));
            xml.writeAttribute(wordNamespace(), QStringLiteral("numId"), QString::number(identifier));
            writeValElement(
                xml, QStringLiteral("abstractNumId"), QString::number(identifier));
            xml.writeEndElement();
        }
        xml.writeEndElement();
    });
}

QByteArray packageRelationshipsXml()
{
    return xmlDocument([](QXmlStreamWriter& xml) {
        xml.writeStartElement(QStringLiteral("Relationships"));
        xml.writeDefaultNamespace(relationshipNamespace());
        const auto relationship = [&](const QString& id, const QString& type, const QString& target) {
            xml.writeStartElement(relationshipNamespace(), QStringLiteral("Relationship"));
            xml.writeAttribute(QStringLiteral("Id"), id);
            xml.writeAttribute(QStringLiteral("Type"), type);
            xml.writeAttribute(QStringLiteral("Target"), target);
            xml.writeEndElement();
        };
        relationship(
            QStringLiteral("rId1"),
            QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument"),
            QStringLiteral("word/document.xml"));
        relationship(
            QStringLiteral("rId2"),
            QStringLiteral("http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties"),
            QStringLiteral("docProps/core.xml"));
        relationship(
            QStringLiteral("rId3"),
            QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/extended-properties"),
            QStringLiteral("docProps/app.xml"));
        xml.writeEndElement();
    });
}

QByteArray documentRelationshipsXml(bool hasNumbering)
{
    return xmlDocument([&](QXmlStreamWriter& xml) {
        xml.writeStartElement(QStringLiteral("Relationships"));
        xml.writeDefaultNamespace(relationshipNamespace());
        xml.writeStartElement(relationshipNamespace(), QStringLiteral("Relationship"));
        xml.writeAttribute(QStringLiteral("Id"), QStringLiteral("rId1"));
        xml.writeAttribute(
            QStringLiteral("Type"),
            QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles"));
        xml.writeAttribute(QStringLiteral("Target"), QStringLiteral("styles.xml"));
        xml.writeEndElement();
        if (hasNumbering) {
            xml.writeStartElement(relationshipNamespace(), QStringLiteral("Relationship"));
            xml.writeAttribute(QStringLiteral("Id"), QStringLiteral("rId2"));
            xml.writeAttribute(
                QStringLiteral("Type"),
                QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/numbering"));
            xml.writeAttribute(QStringLiteral("Target"), QStringLiteral("numbering.xml"));
            xml.writeEndElement();
        }
        xml.writeEndElement();
    });
}

QByteArray contentTypesXml(bool hasNumbering)
{
    static const QString contentTypesNamespace =
        QStringLiteral("http://schemas.openxmlformats.org/package/2006/content-types");
    return xmlDocument([&](QXmlStreamWriter& xml) {
        xml.writeStartElement(QStringLiteral("Types"));
        xml.writeDefaultNamespace(contentTypesNamespace);
        const auto defaultType = [&](const QString& extension, const QString& contentType) {
            xml.writeStartElement(contentTypesNamespace, QStringLiteral("Default"));
            xml.writeAttribute(QStringLiteral("Extension"), extension);
            xml.writeAttribute(QStringLiteral("ContentType"), contentType);
            xml.writeEndElement();
        };
        const auto overrideType = [&](const QString& partName, const QString& contentType) {
            xml.writeStartElement(contentTypesNamespace, QStringLiteral("Override"));
            xml.writeAttribute(QStringLiteral("PartName"), partName);
            xml.writeAttribute(QStringLiteral("ContentType"), contentType);
            xml.writeEndElement();
        };
        defaultType(
            QStringLiteral("rels"),
            QStringLiteral("application/vnd.openxmlformats-package.relationships+xml"));
        defaultType(QStringLiteral("xml"), QStringLiteral("application/xml"));
        overrideType(
            QStringLiteral("/word/document.xml"),
            QStringLiteral("application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"));
        overrideType(
            QStringLiteral("/word/styles.xml"),
            QStringLiteral("application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml"));
        if (hasNumbering) {
            overrideType(
                QStringLiteral("/word/numbering.xml"),
                QStringLiteral("application/vnd.openxmlformats-officedocument.wordprocessingml.numbering+xml"));
        }
        overrideType(
            QStringLiteral("/docProps/core.xml"),
            QStringLiteral("application/vnd.openxmlformats-package.core-properties+xml"));
        overrideType(
            QStringLiteral("/docProps/app.xml"),
            QStringLiteral("application/vnd.openxmlformats-officedocument.extended-properties+xml"));
        xml.writeEndElement();
    });
}

QByteArray corePropertiesXml(const WordDocument& document)
{
    static const QString cpNamespace =
        QStringLiteral("http://schemas.openxmlformats.org/package/2006/metadata/core-properties");
    static const QString dcNamespace = QStringLiteral("http://purl.org/dc/elements/1.1/");
    static const QString dctermsNamespace = QStringLiteral("http://purl.org/dc/terms/");
    static const QString xsiNamespace = QStringLiteral("http://www.w3.org/2001/XMLSchema-instance");

    return xmlDocument([&](QXmlStreamWriter& xml) {
        xml.writeStartElement(QStringLiteral("cp:coreProperties"));
        xml.writeNamespace(cpNamespace, QStringLiteral("cp"));
        xml.writeNamespace(dcNamespace, QStringLiteral("dc"));
        xml.writeNamespace(dctermsNamespace, QStringLiteral("dcterms"));
        xml.writeNamespace(xsiNamespace, QStringLiteral("xsi"));
        const auto writeMetadata = [&](const char* key, const QString& ns, const QString& name) {
            const auto found = document.metadata().find(key);
            if (found != document.metadata().end() && !found->second.empty()) {
                xml.writeTextElement(ns, name, QString::fromUtf8(found->second));
            }
        };
        writeMetadata("Title", dcNamespace, QStringLiteral("title"));
        writeMetadata("Author", dcNamespace, QStringLiteral("creator"));
        writeMetadata("Subject", dcNamespace, QStringLiteral("subject"));
        writeMetadata("Description", dcNamespace, QStringLiteral("description"));
        writeMetadata("Keywords", cpNamespace, QStringLiteral("keywords"));
        writeMetadata("LastModifiedBy", cpNamespace, QStringLiteral("lastModifiedBy"));
        const auto writeDate = [&](const char* key, const QString& name) {
            const auto found = document.metadata().find(key);
            if (found != document.metadata().end() && !found->second.empty()) {
                xml.writeStartElement(dctermsNamespace, name);
                xml.writeAttribute(xsiNamespace, QStringLiteral("type"), QStringLiteral("dcterms:W3CDTF"));
                xml.writeCharacters(QString::fromUtf8(found->second));
                xml.writeEndElement();
            }
        };
        writeDate("Created", QStringLiteral("created"));
        writeDate("Modified", QStringLiteral("modified"));
        xml.writeEndElement();
    });
}

QByteArray appPropertiesXml()
{
    static const QString appNamespace =
        QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/extended-properties");
    return xmlDocument([](QXmlStreamWriter& xml) {
        xml.writeStartElement(QStringLiteral("Properties"));
        xml.writeDefaultNamespace(appNamespace);
        xml.writeNamespace(
            QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/docPropsVTypes"),
            QStringLiteral("vt"));
        xml.writeTextElement(appNamespace, QStringLiteral("Application"), QStringLiteral("iiGeneralDocument"));
        xml.writeTextElement(appNamespace, QStringLiteral("AppVersion"), QStringLiteral("0.1"));
        xml.writeEndElement();
    });
}

struct PackagePart {
    std::string name;
    QByteArray bytes;
};

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

void validateDocument(
    const WordDocument& document,
    const std::filesystem::path& destination,
    std::vector<Diagnostic>& diagnostics)
{
    for (const auto& block : document.blocks()) {
        if (const auto* paragraph = std::get_if<WordParagraph>(&block)) {
            validateParagraph(*paragraph, destination, diagnostics);
            continue;
        }
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
                for (const auto& paragraph : cell.paragraphs) {
                    validateParagraph(paragraph, destination, diagnostics);
                }
            }
        }
    }
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
    ZipArchive archive(zip_open(source.string().c_str(), ZIP_RDONLY, &openError));
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
    return result;
}

WordWriteResult writeDocxPackage(
    const WordDocument& document,
    const std::filesystem::path& destination,
    const WordWriteOptions& options)
{
    (void)options;
    WordWriteResult result;
    if (destination.empty()) {
        result.diagnostics.push_back(diagnostic(
            DiagnosticSeverity::error,
            "docx.destination_missing",
            "A DOCX destination path is required.",
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
        std::filesystem::create_directories(destination.parent_path(), directoryError);
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
    parts.push_back({"docProps/app.xml", appPropertiesXml()});
    parts.push_back({"word/document.xml", documentXml(document)});
    parts.push_back({"word/styles.xml", stylesXml(document)});
    parts.push_back({"word/_rels/document.xml.rels", documentRelationshipsXml(hasNumbering)});
    if (hasNumbering) {
        parts.push_back({"word/numbering.xml", numberingXml(numberingIdentifiers)});
    }

    int openError = 0;
    ZipArchive archive(zip_open(
        destination.string().c_str(), ZIP_CREATE | ZIP_TRUNCATE, &openError));
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
    auto validation = readDocxPackage(destination, validationOptions);
    if (validation.hasErrors()) {
        result.diagnostics.push_back(diagnostic(
            DiagnosticSeverity::error,
            "docx.post_write_validation_failed",
            "The committed DOCX package could not be reopened.",
            destination));
        result.diagnostics.insert(
            result.diagnostics.end(),
            std::make_move_iterator(validation.diagnostics.begin()),
            std::make_move_iterator(validation.diagnostics.end()));
    }
    return result;
}

} // namespace ii::document::detail
