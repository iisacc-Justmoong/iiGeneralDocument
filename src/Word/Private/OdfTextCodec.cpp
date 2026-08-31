#include "Word/Private/OdfTextCodec.h"

#include "Word/Private/AtomicFileCommit.h"

#include <QBuffer>
#include <QByteArray>
#include <QFile>
#include <QString>
#include <QTemporaryFile>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <zip.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace ii::document::detail {
namespace {

constexpr std::string_view odtMimeType =
    "application/vnd.oasis.opendocument.text";
constexpr std::uint64_t maximumExpandedItems = 1'000'000;
constexpr std::uint32_t maximumSemanticNestingDepth = 128;
constexpr zip_int64_t maximumPackageEntries = 10'000;
constexpr std::size_t maximumPackageEntryNameBytes = 4'096;

bool safePackagePath(std::string_view name);

const QString& officeNamespace()
{
    static const QString value =
        QStringLiteral("urn:oasis:names:tc:opendocument:xmlns:office:1.0");
    return value;
}

const QString& textNamespace()
{
    static const QString value =
        QStringLiteral("urn:oasis:names:tc:opendocument:xmlns:text:1.0");
    return value;
}

const QString& styleNamespace()
{
    static const QString value =
        QStringLiteral("urn:oasis:names:tc:opendocument:xmlns:style:1.0");
    return value;
}

const QString& tableNamespace()
{
    static const QString value =
        QStringLiteral("urn:oasis:names:tc:opendocument:xmlns:table:1.0");
    return value;
}

const QString& manifestNamespace()
{
    static const QString value =
        QStringLiteral("urn:oasis:names:tc:opendocument:xmlns:manifest:1.0");
    return value;
}

const QString& metaNamespace()
{
    static const QString value =
        QStringLiteral("urn:oasis:names:tc:opendocument:xmlns:meta:1.0");
    return value;
}

const QString& dcNamespace()
{
    static const QString value = QStringLiteral("http://purl.org/dc/elements/1.1/");
    return value;
}

const QString& foNamespace()
{
    static const QString value =
        QStringLiteral("urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0");
    return value;
}

const QString& svgNamespace()
{
    static const QString value =
        QStringLiteral("urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0");
    return value;
}

const QString& xmlNamespace()
{
    static const QString value = QStringLiteral("http://www.w3.org/XML/1998/namespace");
    return value;
}

std::string toUtf8(const QString& value)
{
    const auto bytes = value.toUtf8();
    return {bytes.constData(), static_cast<std::size_t>(bytes.size())};
}

QString fromUtf8(const std::string& value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

QString attribute(
    const QXmlStreamReader& xml,
    const QString& namespaceUri,
    const QString& localName)
{
    return xml.attributes().value(namespaceUri, localName).toString();
}

bool isElement(
    const QXmlStreamReader& xml,
    const QString& namespaceUri,
    const QString& localName)
{
    return xml.isStartElement() && xml.namespaceUri() == namespaceUri
        && xml.name() == localName;
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
    if (std::ranges::none_of(diagnostics, [&](const Diagnostic& item) {
            return item.code == code;
        })) {
        diagnostics.push_back(diagnostic(
            DiagnosticSeverity::warning, std::move(code), std::move(message), context));
    }
}

void errorOnce(
    std::vector<Diagnostic>& diagnostics,
    std::string code,
    std::string message,
    const std::filesystem::path& context)
{
    if (std::ranges::none_of(diagnostics, [&](const Diagnostic& item) {
            return item.code == code;
        })) {
        diagnostics.push_back(diagnostic(
            DiagnosticSeverity::error, std::move(code), std::move(message), context));
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

std::optional<QByteArray> readZipPart(
    zip_t* archive,
    std::string_view name,
    std::uint64_t maximumBytes,
    const std::filesystem::path& source,
    std::vector<Diagnostic>& diagnostics,
    bool required = true)
{
    const std::string partName{name};
    zip_stat_t status;
    zip_stat_init(&status);
    if (zip_stat(archive, partName.c_str(), ZIP_FL_ENC_UTF_8, &status) != 0) {
        if (required) {
            diagnostics.push_back(diagnostic(
                DiagnosticSeverity::error,
                "odf.missing_part",
                "The ODT package is missing required part " + partName + ".",
                source));
        }
        return std::nullopt;
    }
    if ((status.valid & ZIP_STAT_SIZE) == 0U || status.size > maximumBytes
        || status.size > static_cast<zip_uint64_t>(std::numeric_limits<qsizetype>::max())) {
        diagnostics.push_back(diagnostic(
            DiagnosticSeverity::error,
            "odf.part_too_large",
            "The OpenDocument XML part exceeds the configured read limit: "
                + partName + ".",
            source));
        return std::nullopt;
    }

    zip_file_t* file = zip_fopen(archive, partName.c_str(), ZIP_FL_ENC_UTF_8);
    if (!file) {
        diagnostics.push_back(diagnostic(
            DiagnosticSeverity::error,
            "odf.part_open_failed",
            "The ODT part could not be opened: " + partName + ".",
            source));
        return std::nullopt;
    }

    QByteArray bytes(static_cast<qsizetype>(status.size), Qt::Uninitialized);
    zip_uint64_t offset = 0;
    while (offset < status.size) {
        const auto count = zip_fread(
            file,
            bytes.data() + static_cast<qsizetype>(offset),
            status.size - offset);
        if (count < 0) {
            const std::string message = zip_file_strerror(file);
            zip_fclose(file);
            diagnostics.push_back(diagnostic(
                DiagnosticSeverity::error,
                "odf.part_read_failed",
                "The ODT part could not be read: " + partName + ": " + message,
                source));
            return std::nullopt;
        }
        if (count == 0) {
            break;
        }
        offset += static_cast<zip_uint64_t>(count);
    }
    if (offset != status.size) {
        zip_fclose(file);
        diagnostics.push_back(diagnostic(
            DiagnosticSeverity::error,
            "odf.part_truncated",
            "The ODT part ended before its declared size: " + partName + ".",
            source));
        return std::nullopt;
    }

    char probe{};
    const auto probeCount = zip_fread(file, &probe, 1);
    if (probeCount < 0) {
        const std::string message = zip_file_strerror(file);
        zip_fclose(file);
        diagnostics.push_back(diagnostic(
            DiagnosticSeverity::error,
            "odf.part_integrity_failed",
            "The ODT part failed its ZIP integrity check: " + partName + ": "
                + message,
            source));
        return std::nullopt;
    }
    if (probeCount != 0) {
        zip_fclose(file);
        diagnostics.push_back(diagnostic(
            DiagnosticSeverity::error,
            "odf.part_size_mismatch",
            "The ODT part contains data beyond its declared size: " + partName + ".",
            source));
        return std::nullopt;
    }
    if (zip_fclose(file) != 0) {
        diagnostics.push_back(diagnostic(
            DiagnosticSeverity::error,
            "odf.part_close_failed",
            "The ODT part could not complete its integrity check: " + partName + ".",
            source));
        return std::nullopt;
    }
    return bytes;
}

std::optional<QByteArray> readFlatXml(
    const std::filesystem::path& source,
    const WordReadOptions& options,
    std::vector<Diagnostic>& diagnostics)
{
    if (!std::filesystem::is_regular_file(source)) {
        diagnostics.push_back(diagnostic(
            DiagnosticSeverity::error,
            "fodt.source_missing",
            "The FODT source does not exist.",
            source));
        return std::nullopt;
    }
    std::error_code sizeError;
    const auto size = std::filesystem::file_size(source, sizeError);
    if (sizeError || options.maximumXmlPartBytes == 0
        || size > options.maximumXmlPartBytes
        || size > static_cast<std::uint64_t>(std::numeric_limits<qsizetype>::max())) {
        diagnostics.push_back(diagnostic(
            DiagnosticSeverity::error,
            "fodt.part_too_large",
            "The FODT XML exceeds the configured read limit.",
            source));
        return std::nullopt;
    }

    QFile file(QString::fromStdString(source.string()));
    if (!file.open(QIODevice::ReadOnly)) {
        diagnostics.push_back(diagnostic(
            DiagnosticSeverity::error,
            "fodt.open_failed",
            "The FODT XML could not be opened: " + toUtf8(file.errorString()),
            source));
        return std::nullopt;
    }
    const auto boundedLimit = std::min(
        options.maximumXmlPartBytes,
        static_cast<std::uint64_t>(std::numeric_limits<qint64>::max() - 1));
    auto bytes = file.read(static_cast<qint64>(boundedLimit + 1));
    if (file.error() != QFileDevice::NoError
        || static_cast<std::uint64_t>(bytes.size()) > options.maximumXmlPartBytes) {
        diagnostics.push_back(diagnostic(
            DiagnosticSeverity::error,
            "fodt.part_too_large",
            "The FODT XML changed while reading or exceeds the configured read limit.",
            source));
        return std::nullopt;
    }
    return bytes;
}

bool validateXmlEnvelope(
    const QByteArray& bytes,
    const QString& expectedRoot,
    bool requireMimeType,
    std::string_view partName,
    const std::filesystem::path& source,
    std::vector<Diagnostic>& diagnostics)
{
    QXmlStreamReader xml(bytes);
    bool foundRoot = false;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.tokenType() == QXmlStreamReader::DTD) {
            diagnostics.push_back(diagnostic(
                DiagnosticSeverity::error,
                "odf.dtd_not_allowed",
                "OpenDocument XML must not contain a DTD: " + std::string(partName) + ".",
                source));
            return false;
        }
        if (!xml.isStartElement()) {
            continue;
        }
        foundRoot = true;
        if (xml.namespaceUri() != officeNamespace() || xml.name() != expectedRoot) {
            diagnostics.push_back(diagnostic(
                DiagnosticSeverity::error,
                "odf.invalid_root",
                "The OpenDocument XML root is invalid for " + std::string(partName) + ".",
                source));
            return false;
        }
        const auto version = attribute(
            xml, officeNamespace(), QStringLiteral("version"));
        static const std::set<QString> supportedVersions{
            QStringLiteral("1.0"), QStringLiteral("1.1"), QStringLiteral("1.2"),
            QStringLiteral("1.3"), QStringLiteral("1.4")};
        if (!version.isEmpty() && !supportedVersions.contains(version)) {
            warningOnce(
                diagnostics,
                "odf.unknown_version",
                "The OpenDocument version is newer or unknown; supported content is read best-effort.",
                source);
        }
        if (requireMimeType) {
            const auto mediaType = attribute(
                xml, officeNamespace(), QStringLiteral("mimetype"));
            if (mediaType != QString::fromLatin1(odtMimeType.data(),
                                                  static_cast<qsizetype>(odtMimeType.size()))) {
                diagnostics.push_back(diagnostic(
                    DiagnosticSeverity::error,
                    "fodt.invalid_mimetype",
                    "The flat OpenDocument root does not declare the text media type.",
                    source));
                return false;
            }
        }
        break;
    }
    if (xml.hasError() || !foundRoot) {
        diagnostics.push_back(diagnostic(
            DiagnosticSeverity::error,
            "odf.xml_error",
            "The OpenDocument XML is malformed in " + std::string(partName) + ": "
                + toUtf8(xml.errorString()),
            source));
        return false;
    }

    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.tokenType() == QXmlStreamReader::DTD) {
            diagnostics.push_back(diagnostic(
                DiagnosticSeverity::error,
                "odf.dtd_not_allowed",
                "OpenDocument XML must not contain a DTD: " + std::string(partName) + ".",
                source));
            return false;
        }
    }
    if (xml.hasError()) {
        diagnostics.push_back(diagnostic(
            DiagnosticSeverity::error,
            "odf.xml_error",
            "The OpenDocument XML is malformed in " + std::string(partName) + ": "
                + toUtf8(xml.errorString()),
            source));
        return false;
    }
    return true;
}

struct StyleDefinition {
    std::string family;
    std::string parent;
    std::string masterPageName;
    std::optional<std::string> listStyleName;
    std::optional<bool> bold;
    std::optional<bool> italic;
    std::optional<bool> underline;
    std::optional<std::string> fontFamily;
    std::optional<std::string> eastAsiaFontFamily;
    std::optional<double> fontSizePoints;
    std::optional<std::string> color;
    std::optional<WordParagraphAlignment> alignment;
};

struct StyleKey {
    std::string family;
    std::string name;

    [[nodiscard]] bool operator<(const StyleKey& other) const noexcept
    {
        if (family != other.family) {
            return family < other.family;
        }
        return name < other.name;
    }
};

using StyleMap = std::map<StyleKey, StyleDefinition>;
using FontFaceMap = std::map<std::string, std::string>;

struct PageStyleCatalog {
    std::map<std::string, WordSectionProperties> layouts;
    std::map<std::string, std::string> masterPageLayouts;
    std::optional<std::string> firstPageLayoutName;
    std::optional<std::string> firstMasterPageName;
};

std::string strippedFontFamily(QString value)
{
    value = value.trimmed();
    if (value.size() >= 2
        && ((value.front() == QLatin1Char('\'') && value.back() == QLatin1Char('\''))
            || (value.front() == QLatin1Char('"') && value.back() == QLatin1Char('"')))) {
        value = value.mid(1, value.size() - 2);
    }
    return toUtf8(value);
}

std::optional<double> pointValue(const QString& value)
{
    auto text = value.trimmed().toLower();
    double multiplier = 1.0;
    if (text.endsWith(QStringLiteral("pt"))) {
        text.chop(2);
    } else if (text.endsWith(QStringLiteral("in"))) {
        text.chop(2);
        multiplier = 72.0;
    } else if (text.endsWith(QStringLiteral("cm"))) {
        text.chop(2);
        multiplier = 72.0 / 2.54;
    } else if (text.endsWith(QStringLiteral("mm"))) {
        text.chop(2);
        multiplier = 72.0 / 25.4;
    } else if (text.endsWith(QStringLiteral("pc"))) {
        text.chop(2);
        multiplier = 12.0;
    } else {
        return std::nullopt;
    }
    bool valid = false;
    const auto number = text.toDouble(&valid);
    if (!valid || !std::isfinite(number)) {
        return std::nullopt;
    }
    return number * multiplier;
}

std::optional<int> twipsValue(const QString& value)
{
    const auto points = pointValue(value);
    if (!points || *points < 0.0
        || *points > static_cast<double>(std::numeric_limits<int>::max()) / 20.0) {
        return std::nullopt;
    }
    return static_cast<int>(std::lround(*points * 20.0));
}

void parseTextProperties(QXmlStreamReader& xml, StyleDefinition& style)
{
    const auto weight = attribute(xml, foNamespace(), QStringLiteral("font-weight"));
    if (!weight.isEmpty()) {
        style.bold = weight.compare(QStringLiteral("bold"), Qt::CaseInsensitive) == 0
            || weight.toInt() >= 600;
    }
    const auto fontStyle = attribute(xml, foNamespace(), QStringLiteral("font-style"));
    if (!fontStyle.isEmpty()) {
        style.italic = fontStyle.compare(QStringLiteral("italic"), Qt::CaseInsensitive) == 0
            || fontStyle.compare(QStringLiteral("oblique"), Qt::CaseInsensitive) == 0;
    }
    const auto underline = attribute(
        xml, styleNamespace(), QStringLiteral("text-underline-style"));
    if (!underline.isEmpty()) {
        style.underline = underline.compare(QStringLiteral("none"), Qt::CaseInsensitive) != 0;
    }
    auto font = attribute(xml, foNamespace(), QStringLiteral("font-family"));
    if (font.isEmpty()) {
        font = attribute(xml, styleNamespace(), QStringLiteral("font-name"));
    }
    if (!font.isEmpty()) {
        style.fontFamily = strippedFontFamily(font);
    }
    const auto asianFont = attribute(
        xml, styleNamespace(), QStringLiteral("font-name-asian"));
    if (!asianFont.isEmpty()) {
        style.eastAsiaFontFamily = strippedFontFamily(asianFont);
    }
    const auto size = pointValue(attribute(
        xml, foNamespace(), QStringLiteral("font-size")));
    if (size && *size >= 0.0) {
        style.fontSizePoints = *size;
    }
    auto color = attribute(xml, foNamespace(), QStringLiteral("color")).trimmed();
    if (color.startsWith(QLatin1Char('#'))) {
        color.remove(0, 1);
    }
    if (color.size() == 6) {
        style.color = toUtf8(color.toUpper());
    }
    xml.skipCurrentElement();
}

void parseParagraphProperties(QXmlStreamReader& xml, StyleDefinition& style)
{
    const auto alignment = attribute(
        xml, foNamespace(), QStringLiteral("text-align")).toLower();
    if (alignment == QStringLiteral("left") || alignment == QStringLiteral("start")) {
        style.alignment = WordParagraphAlignment::left;
    } else if (alignment == QStringLiteral("center")) {
        style.alignment = WordParagraphAlignment::center;
    } else if (alignment == QStringLiteral("right") || alignment == QStringLiteral("end")) {
        style.alignment = WordParagraphAlignment::right;
    } else if (alignment == QStringLiteral("justify")) {
        style.alignment = WordParagraphAlignment::justified;
    }
    xml.skipCurrentElement();
}

StyleDefinition parseStyleElement(QXmlStreamReader& xml)
{
    StyleDefinition style;
    style.family = toUtf8(attribute(xml, styleNamespace(), QStringLiteral("family")));
    style.parent = toUtf8(attribute(
        xml, styleNamespace(), QStringLiteral("parent-style-name")));
    style.masterPageName = toUtf8(attribute(
        xml, styleNamespace(), QStringLiteral("master-page-name")));
    const auto listStyleName = xml.attributes().value(
        styleNamespace(), QStringLiteral("list-style-name"));
    if (!listStyleName.isNull()) {
        style.listStyleName = toUtf8(listStyleName.toString());
    }
    while (xml.readNextStartElement()) {
        if (isElement(xml, styleNamespace(), QStringLiteral("text-properties"))) {
            parseTextProperties(xml, style);
        } else if (isElement(xml, styleNamespace(), QStringLiteral("paragraph-properties"))) {
            parseParagraphProperties(xml, style);
        } else {
            xml.skipCurrentElement();
        }
    }
    return style;
}

WordSectionProperties parsePageLayout(
    QXmlStreamReader& xml,
    const WordSectionProperties& defaults)
{
    auto section = defaults;
    while (xml.readNextStartElement()) {
        if (!isElement(xml, styleNamespace(), QStringLiteral("page-layout-properties"))) {
            xml.skipCurrentElement();
            continue;
        }
        const auto assignPositive = [&](const QString& name, int& target) {
            const auto parsed = twipsValue(attribute(xml, foNamespace(), name));
            if (parsed && *parsed > 0) {
                target = *parsed;
            }
        };
        const auto assignNonNegative = [&](const QString& name, int& target) {
            const auto parsed = twipsValue(attribute(xml, foNamespace(), name));
            if (parsed && *parsed >= 0) {
                target = *parsed;
            }
        };
        assignPositive(QStringLiteral("page-width"), section.pageWidthTwips);
        assignPositive(QStringLiteral("page-height"), section.pageHeightTwips);
        if (const auto margin = twipsValue(attribute(
                xml, foNamespace(), QStringLiteral("margin")));
            margin && *margin >= 0) {
            section.marginTopTwips = *margin;
            section.marginRightTwips = *margin;
            section.marginBottomTwips = *margin;
            section.marginLeftTwips = *margin;
        }
        assignNonNegative(QStringLiteral("margin-top"), section.marginTopTwips);
        assignNonNegative(QStringLiteral("margin-right"), section.marginRightTwips);
        assignNonNegative(QStringLiteral("margin-bottom"), section.marginBottomTwips);
        assignNonNegative(QStringLiteral("margin-left"), section.marginLeftTwips);
        xml.skipCurrentElement();
    }
    return section;
}

FontFaceMap parseStyles(
    const QByteArray& bytes,
    StyleMap& styles,
    PageStyleCatalog& pageStyles,
    const WordSectionProperties& defaultSection,
    const FontFaceMap& fallbackFontFaces)
{
    FontFaceMap fontFaces;
    StyleMap parsedStyles;
    QXmlStreamReader xml(bytes);
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement()) {
            continue;
        }
        if (isElement(xml, styleNamespace(), QStringLiteral("font-face"))) {
            const auto name = toUtf8(attribute(
                xml, styleNamespace(), QStringLiteral("name")));
            const auto family = strippedFontFamily(attribute(
                xml, svgNamespace(), QStringLiteral("font-family")));
            if (!name.empty() && !family.empty()) {
                fontFaces[name] = family;
            }
            xml.skipCurrentElement();
        } else if (isElement(xml, styleNamespace(), QStringLiteral("style"))) {
            const auto name = toUtf8(attribute(
                xml, styleNamespace(), QStringLiteral("name")));
            auto style = parseStyleElement(xml);
            if (!name.empty()) {
                const StyleKey key{style.family, name};
                parsedStyles[key] = std::move(style);
            }
        } else if (isElement(xml, styleNamespace(), QStringLiteral("page-layout"))) {
            const auto name = toUtf8(attribute(
                xml, styleNamespace(), QStringLiteral("name")));
            auto layout = parsePageLayout(xml, defaultSection);
            if (!name.empty()) {
                if (!pageStyles.firstPageLayoutName) {
                    pageStyles.firstPageLayoutName = name;
                }
                pageStyles.layouts[name] = std::move(layout);
            }
        } else if (isElement(xml, styleNamespace(), QStringLiteral("master-page"))) {
            const auto name = toUtf8(attribute(
                xml, styleNamespace(), QStringLiteral("name")));
            const auto layoutName = toUtf8(attribute(
                xml, styleNamespace(), QStringLiteral("page-layout-name")));
            if (!name.empty() && !layoutName.empty()) {
                if (!pageStyles.firstMasterPageName) {
                    pageStyles.firstMasterPageName = name;
                }
                pageStyles.masterPageLayouts[name] = layoutName;
            }
            xml.skipCurrentElement();
        }
    }
    const auto resolveFontFace = [&](std::optional<std::string>& fontFamily) {
        if (!fontFamily) {
            return;
        }
        if (const auto local = fontFaces.find(*fontFamily); local != fontFaces.end()) {
            fontFamily = local->second;
            return;
        }
        if (const auto fallback = fallbackFontFaces.find(*fontFamily);
            fallback != fallbackFontFaces.end()) {
            fontFamily = fallback->second;
        }
    };
    for (auto& [key, style] : parsedStyles) {
        resolveFontFace(style.fontFamily);
        resolveFontFace(style.eastAsiaFontFamily);
        styles[key] = std::move(style);
    }
    return fontFaces;
}

void overlayStyle(StyleDefinition& base, const StyleDefinition& overlay)
{
    if (!overlay.masterPageName.empty()) base.masterPageName = overlay.masterPageName;
    if (overlay.listStyleName) base.listStyleName = overlay.listStyleName;
    if (overlay.bold) base.bold = overlay.bold;
    if (overlay.italic) base.italic = overlay.italic;
    if (overlay.underline) base.underline = overlay.underline;
    if (overlay.fontFamily) base.fontFamily = overlay.fontFamily;
    if (overlay.eastAsiaFontFamily) base.eastAsiaFontFamily = overlay.eastAsiaFontFamily;
    if (overlay.fontSizePoints) base.fontSizePoints = overlay.fontSizePoints;
    if (overlay.color) base.color = overlay.color;
    if (overlay.alignment) base.alignment = overlay.alignment;
}

StyleDefinition resolveStyle(
    const std::string& family,
    const std::string& name,
    const StyleMap& styles,
    std::set<StyleKey>& resolving,
    const std::filesystem::path& source,
    std::vector<Diagnostic>& diagnostics,
    std::uint32_t depth)
{
    if (depth > maximumSemanticNestingDepth) {
        errorOnce(
            diagnostics,
            "odf.nesting_limit_exceeded",
            "The OpenDocument style inheritance depth exceeds the safety limit.",
            source);
        return {};
    }
    const StyleKey key{family, name};
    const auto found = styles.find(key);
    if (found == styles.end()) {
        return {};
    }
    if (!resolving.insert(key).second) {
        warningOnce(
            diagnostics,
            "odf.style_cycle_ignored",
            "A cyclic OpenDocument style inheritance chain was ignored.",
            source);
        return {};
    }
    StyleDefinition result;
    if (!found->second.parent.empty()) {
        result = resolveStyle(
            family, found->second.parent, styles, resolving, source, diagnostics, depth + 1);
    }
    overlayStyle(result, found->second);
    result.family = found->second.family;
    result.parent = found->second.parent;
    resolving.erase(key);
    return result;
}

StyleDefinition resolveStyle(
    const std::string& family,
    const std::string& name,
    const StyleMap& styles,
    const std::filesystem::path& source,
    std::vector<Diagnostic>& diagnostics)
{
    std::set<StyleKey> resolving;
    return resolveStyle(family, name, styles, resolving, source, diagnostics, 0);
}

std::optional<std::string> firstAppliedMasterPage(
    const QByteArray& bytes,
    const StyleMap& styles,
    const std::filesystem::path& source,
    std::vector<Diagnostic>& diagnostics)
{
    QXmlStreamReader xml(bytes);
    while (!xml.atEnd()) {
        xml.readNext();
        if (!isElement(xml, officeNamespace(), QStringLiteral("text"))) {
            continue;
        }

        int depth = 1;
        int tableDepth = 0;
        while (!xml.atEnd() && depth > 0) {
            xml.readNext();
            if (xml.isEndElement()) {
                if (xml.namespaceUri() == tableNamespace()
                    && xml.name() == QStringLiteral("table")
                    && tableDepth > 0) {
                    --tableDepth;
                }
                --depth;
                continue;
            }
            if (!xml.isStartElement()) {
                continue;
            }
            ++depth;

            std::string family;
            QString styleName;
            const bool tableElement = isElement(
                xml, tableNamespace(), QStringLiteral("table"));
            const bool outsideTable = tableDepth == 0;
            if (outsideTable
                && (isElement(xml, textNamespace(), QStringLiteral("p"))
                    || isElement(xml, textNamespace(), QStringLiteral("h")))) {
                family = "paragraph";
                styleName = attribute(
                    xml, textNamespace(), QStringLiteral("style-name"));
            } else if (outsideTable && tableElement) {
                family = "table";
                styleName = attribute(
                    xml, tableNamespace(), QStringLiteral("style-name"));
            }
            if (tableElement) {
                ++tableDepth;
            }

            if (family.empty() || styleName.isEmpty()) {
                continue;
            }
            const auto resolved = resolveStyle(
                family, toUtf8(styleName), styles, source, diagnostics);
            if (!resolved.masterPageName.empty()) {
                return resolved.masterPageName;
            }
        }
        break;
    }
    return std::nullopt;
}

void selectPageLayout(
    const QByteArray& content,
    const StyleMap& styles,
    const PageStyleCatalog& pageStyles,
    WordSectionProperties& section,
    const std::filesystem::path& source,
    std::vector<Diagnostic>& diagnostics)
{
    if (pageStyles.layouts.empty()) {
        return;
    }
    if (pageStyles.layouts.size() > 1
        || pageStyles.masterPageLayouts.size() > 1) {
        warningOnce(
            diagnostics,
            "odf.multiple_page_layouts_flattened",
            "Only one OpenDocument page layout can be represented by the flow model.",
            source);
    }

    auto selected = pageStyles.layouts.end();
    const auto selectMaster = [&](const std::string& masterPageName) {
        const auto master = pageStyles.masterPageLayouts.find(masterPageName);
        if (master == pageStyles.masterPageLayouts.end()) {
            warningOnce(
                diagnostics,
                "odf.master_page_missing",
                "An applied OpenDocument master page could not be resolved.",
                source);
            return pageStyles.layouts.end();
        }
        const auto layout = pageStyles.layouts.find(master->second);
        if (layout == pageStyles.layouts.end()) {
            warningOnce(
                diagnostics,
                "odf.master_page_layout_missing",
                "An OpenDocument master page references a missing page layout.",
                source);
        }
        return layout;
    };

    if (const auto applied = firstAppliedMasterPage(
            content, styles, source, diagnostics)) {
        selected = selectMaster(*applied);
    }
    if (selected == pageStyles.layouts.end()
        && pageStyles.firstMasterPageName) {
        selected = selectMaster(*pageStyles.firstMasterPageName);
    }
    if (selected == pageStyles.layouts.end()
        && pageStyles.firstPageLayoutName) {
        selected = pageStyles.layouts.find(*pageStyles.firstPageLayoutName);
    }
    if (selected != pageStyles.layouts.end()) {
        section = selected->second;
    }
}

bool generatedStyleName(std::string_view name, std::string_view prefix)
{
    if (!name.starts_with(prefix) || name.size() == prefix.size()) {
        return false;
    }
    return std::ranges::all_of(name.substr(prefix.size()), [](unsigned char character) {
        return std::isdigit(character) != 0;
    });
}

void applyStyle(const StyleDefinition& style, WordRunProperties& properties)
{
    if (style.bold) properties.bold = *style.bold;
    if (style.italic) properties.italic = *style.italic;
    if (style.underline) properties.underline = *style.underline;
    if (style.fontFamily) properties.fontFamily = *style.fontFamily;
    if (style.eastAsiaFontFamily) {
        properties.eastAsiaFontFamily = *style.eastAsiaFontFamily;
    }
    if (style.fontSizePoints) properties.fontSizePoints = *style.fontSizePoints;
    if (style.color) properties.color = *style.color;
}

bool sameRunProperties(const WordRunProperties& left, const WordRunProperties& right)
{
    return left.bold == right.bold && left.italic == right.italic
        && left.underline == right.underline && left.fontFamily == right.fontFamily
        && left.eastAsiaFontFamily == right.eastAsiaFontFamily
        && left.fontSizePoints == right.fontSizePoints && left.color == right.color;
}

struct ReadContext {
    const StyleMap& styles;
    const std::filesystem::path& source;
    std::vector<Diagnostic>& diagnostics;
    std::map<std::string, int> listElementIdentifiers;
    std::optional<int> lastTopLevelListIdentifier;
    std::optional<std::string> lastTopLevelListStyleName;
    int nextListIdentifier{1};
    std::uint64_t expandedItems{0};
    std::uint64_t expandedCharacters{0};
    std::uint64_t maximumExpandedCharacters{0};
    bool expansionFailed{false};
};

bool consumeItems(ReadContext& context, std::uint64_t count)
{
    if (count > maximumExpandedItems - std::min(
            context.expandedItems, maximumExpandedItems)) {
        errorOnce(
            context.diagnostics,
            "odf.model_expansion_limit_exceeded",
            "The expanded OpenDocument object count exceeds the safety limit.",
            context.source);
        context.expansionFailed = true;
        return false;
    }
    context.expandedItems += count;
    return true;
}

bool consumeCharacters(ReadContext& context, std::uint64_t count)
{
    if (count > context.maximumExpandedCharacters
            - std::min(context.expandedCharacters, context.maximumExpandedCharacters)) {
        errorOnce(
            context.diagnostics,
            "odf.text_expansion_limit_exceeded",
            "The expanded OpenDocument text exceeds the configured XML safety budget.",
            context.source);
        context.expansionFailed = true;
        return false;
    }
    context.expandedCharacters += count;
    return true;
}

bool allowNesting(ReadContext& context, std::uint32_t depth)
{
    if (depth <= maximumSemanticNestingDepth) {
        return true;
    }
    errorOnce(
        context.diagnostics,
        "odf.nesting_limit_exceeded",
        "The OpenDocument semantic nesting depth exceeds the safety limit.",
        context.source);
    context.expansionFailed = true;
    return false;
}

void abortExpansion(QXmlStreamReader& xml, ReadContext& context)
{
    context.expansionFailed = true;
    if (!xml.hasError()) {
        xml.raiseError(QStringLiteral(
            "OpenDocument expansion safety limit exceeded."));
    }
}

bool appendRunText(
    WordParagraph& paragraph,
    const QString& text,
    const WordRunProperties& properties,
    ReadContext& context,
    bool& forceNewRun)
{
    if (text.isEmpty()) {
        return true;
    }
    const auto encoded = toUtf8(text);
    if (!consumeCharacters(context, static_cast<std::uint64_t>(encoded.size()))) {
        return false;
    }
    const bool createRun = forceNewRun || paragraph.runs.empty()
        || !sameRunProperties(paragraph.runs.back().properties, properties);
    if (createRun && !consumeItems(context, 1)) {
        return false;
    }
    if (createRun) {
        paragraph.runs.push_back({encoded, properties});
    } else {
        paragraph.runs.back().text += encoded;
    }
    forceNewRun = false;
    return true;
}

bool isCollapsibleOdfWhitespace(QChar character)
{
    return character == QLatin1Char(' ') || character == QLatin1Char('\n')
        || character == QLatin1Char('\r') || character == QLatin1Char('\t');
}

struct OdfWhitespaceState {
    bool hasOutput{false};
    bool pendingSpace{false};
};

QString consumePendingOdfSpace(OdfWhitespaceState& state)
{
    QString result;
    if (state.pendingSpace && state.hasOutput) {
        result = QLatin1Char(' ');
    }
    state.pendingSpace = false;
    return result;
}

QString normalizedOdfCharacters(
    const QString& value,
    OdfWhitespaceState& state)
{
    if (value.isEmpty()) {
        return {};
    }
    QString result;
    for (const auto character : value) {
        if (isCollapsibleOdfWhitespace(character)) {
            state.pendingSpace = state.hasOutput;
        } else {
            result += consumePendingOdfSpace(state);
            result += character;
            state.hasOutput = true;
        }
    }
    return result;
}

std::uint64_t repeatedCount(
    const QXmlStreamReader& xml,
    const QString& attributeName,
    ReadContext& context)
{
    const auto value = attribute(xml, tableNamespace(), attributeName);
    if (value.isEmpty()) {
        return 1;
    }
    bool valid = false;
    const auto count = value.toULongLong(&valid);
    if (!valid || count == 0 || count > maximumExpandedItems) {
        context.diagnostics.push_back(diagnostic(
            DiagnosticSeverity::error,
            "odf.repetition_limit_exceeded",
            "An OpenDocument repeated row or cell exceeds the expansion safety limit.",
            context.source));
        context.expansionFailed = true;
        return 1;
    }
    return count;
}

void parseInlineElement(
    QXmlStreamReader& xml,
    WordParagraph& paragraph,
    WordRunProperties properties,
    ReadContext& context,
    bool& forceNewRun,
    OdfWhitespaceState& whitespace,
    std::uint32_t depth = 0)
{
    if (!allowNesting(context, depth)) {
        abortExpansion(xml, context);
        return;
    }
    const auto containerNamespace = xml.namespaceUri().toString();
    const auto containerName = xml.name().toString();
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isEndElement() && xml.namespaceUri() == containerNamespace
            && xml.name() == containerName) {
            return;
        }
        if (xml.isCharacters()) {
            if (!appendRunText(
                paragraph,
                normalizedOdfCharacters(xml.text().toString(), whitespace),
                properties,
                context,
                forceNewRun)) {
                abortExpansion(xml, context);
                return;
            }
            continue;
        }
        if (!xml.isStartElement()) {
            continue;
        }

        if (isElement(xml, textNamespace(), QStringLiteral("s"))) {
            auto count = 1ULL;
            const auto countValue = attribute(
                xml, textNamespace(), QStringLiteral("c"));
            if (!countValue.isEmpty()) {
                bool valid = false;
                count = countValue.toULongLong(&valid);
                if (!valid || count == 0 || count > maximumExpandedItems) {
                    context.diagnostics.push_back(diagnostic(
                        DiagnosticSeverity::error,
                        "odf.space_expansion_limit_exceeded",
                        "An OpenDocument explicit-space run exceeds the safety limit.",
                        context.source));
                    abortExpansion(xml, context);
                    return;
                }
            }
            const auto prefix = consumePendingOdfSpace(whitespace);
            const auto totalCount = count + static_cast<std::uint64_t>(prefix.size());
            const auto remainingCharacters = context.maximumExpandedCharacters
                - std::min(
                    context.expandedCharacters, context.maximumExpandedCharacters);
            if (totalCount > remainingCharacters) {
                errorOnce(
                    context.diagnostics,
                    "odf.text_expansion_limit_exceeded",
                    "Explicit OpenDocument spaces exceed the configured XML safety budget.",
                    context.source);
                abortExpansion(xml, context);
                return;
            }
            if (!appendRunText(
                paragraph,
                prefix + QString(static_cast<qsizetype>(count), QLatin1Char(' ')),
                properties,
                context,
                forceNewRun)) {
                abortExpansion(xml, context);
                return;
            }
            whitespace.hasOutput = true;
            xml.skipCurrentElement();
        } else if (isElement(xml, textNamespace(), QStringLiteral("tab"))) {
            const auto value = consumePendingOdfSpace(whitespace)
                + QStringLiteral("\t");
            if (!appendRunText(
                    paragraph, value, properties, context, forceNewRun)) {
                abortExpansion(xml, context);
                return;
            }
            whitespace.hasOutput = true;
            xml.skipCurrentElement();
        } else if (isElement(xml, textNamespace(), QStringLiteral("line-break"))) {
            const auto value = consumePendingOdfSpace(whitespace)
                + QStringLiteral("\n");
            if (!appendRunText(
                    paragraph, value, properties, context, forceNewRun)) {
                abortExpansion(xml, context);
                return;
            }
            whitespace.hasOutput = true;
            xml.skipCurrentElement();
        } else if (isElement(xml, textNamespace(), QStringLiteral("span"))) {
            const auto styleName = toUtf8(attribute(
                xml, textNamespace(), QStringLiteral("style-name")));
            if (!styleName.empty()) {
                applyStyle(
                    resolveStyle(
                        "text", styleName, context.styles,
                        context.source, context.diagnostics),
                    properties);
            }
            bool nestedForceNewRun = true;
            parseInlineElement(
                xml, paragraph, properties, context, nestedForceNewRun,
                whitespace, depth + 1);
            if (context.expansionFailed) {
                return;
            }
            forceNewRun = true;
        } else if (isElement(xml, textNamespace(), QStringLiteral("a"))) {
            warningOnce(
                context.diagnostics,
                "odf.hyperlink_flattened",
                "OpenDocument hyperlink text is readable, but its target is not first-class yet.",
                context.source);
            bool nestedForceNewRun = forceNewRun;
            parseInlineElement(
                xml, paragraph, properties, context, nestedForceNewRun,
                whitespace, depth + 1);
            if (context.expansionFailed) {
                return;
            }
            forceNewRun = nestedForceNewRun;
        } else if (isElement(xml, textNamespace(), QStringLiteral("note"))
                   || isElement(xml, officeNamespace(), QStringLiteral("annotation"))
                   || xml.namespaceUri().toString().contains(QStringLiteral("drawing:1.0"))) {
            warningOnce(
                context.diagnostics,
                "odf.non_text_content_omitted",
                "Notes, annotations, drawings, and embedded objects are not first-class flow items yet.",
                context.source);
            xml.skipCurrentElement();
        } else {
            warningOnce(
                context.diagnostics,
                "odf.unsupported_inline_flattened",
                "An unsupported inline OpenDocument element was flattened to readable text.",
                context.source);
            bool nestedForceNewRun = forceNewRun;
            parseInlineElement(
                xml, paragraph, properties, context, nestedForceNewRun,
                whitespace, depth + 1);
            if (context.expansionFailed) {
                return;
            }
            forceNewRun = nestedForceNewRun;
        }
    }
}

WordParagraph parseParagraph(
    QXmlStreamReader& xml,
    ReadContext& context,
    std::string* sourceStyleName = nullptr)
{
    WordParagraph paragraph;
    if (!consumeItems(context, 1)) {
        abortExpansion(xml, context);
        return paragraph;
    }
    const bool heading = isElement(xml, textNamespace(), QStringLiteral("h"));
    const auto styleName = toUtf8(attribute(
        xml, textNamespace(), QStringLiteral("style-name")));
    if (sourceStyleName != nullptr) {
        *sourceStyleName = styleName;
    }
    const auto resolved = resolveStyle(
        "paragraph", styleName, context.styles, context.source, context.diagnostics);

    if (!styleName.empty()) {
        if (generatedStyleName(styleName, "IGDP") && !resolved.parent.empty()) {
            paragraph.properties.styleId = resolved.parent;
        } else if (!generatedStyleName(styleName, "IGDP")
                   && styleName != "IGDBase") {
            paragraph.properties.styleId = styleName;
        }
    }
    if (heading && paragraph.properties.styleId.empty()) {
        bool valid = false;
        const auto level = attribute(
            xml, textNamespace(), QStringLiteral("outline-level")).toInt(&valid);
        paragraph.properties.styleId = "Heading"
            + std::to_string(valid ? std::clamp(level, 1, 9) : 1);
    }
    if (resolved.alignment) {
        paragraph.properties.alignment = *resolved.alignment;
    }

    WordRunProperties baseProperties;
    applyStyle(resolved, baseProperties);
    bool forceNewRun = true;
    OdfWhitespaceState whitespace;
    parseInlineElement(
        xml, paragraph, baseProperties, context, forceNewRun, whitespace);
    return paragraph;
}

struct ExpansionCost {
    std::uint64_t items{0};
    std::uint64_t characters{0};
};

ExpansionCost modelCost(const WordParagraph& paragraph)
{
    ExpansionCost cost{1, 0};
    cost.items += static_cast<std::uint64_t>(paragraph.runs.size());
    for (const auto& run : paragraph.runs) {
        cost.characters += static_cast<std::uint64_t>(run.text.size());
    }
    return cost;
}

ExpansionCost modelCost(const WordTableCell& cell)
{
    ExpansionCost cost{1, 0};
    for (const auto& paragraph : cell.paragraphs) {
        const auto nested = modelCost(paragraph);
        cost.items += nested.items;
        cost.characters += nested.characters;
    }
    return cost;
}

ExpansionCost modelCost(const WordTableRow& row)
{
    ExpansionCost cost{1, 0};
    for (const auto& cell : row.cells) {
        const auto nested = modelCost(cell);
        cost.items += nested.items;
        cost.characters += nested.characters;
    }
    return cost;
}

bool consumeCopies(
    ReadContext& context,
    const ExpansionCost& cost,
    std::uint64_t copies)
{
    if (copies == 0) {
        return true;
    }
    const auto remainingItems = maximumExpandedItems
        - std::min(context.expandedItems, maximumExpandedItems);
    const auto remainingCharacters = context.maximumExpandedCharacters
        - std::min(context.expandedCharacters, context.maximumExpandedCharacters);
    if ((cost.items != 0 && copies > remainingItems / cost.items)
        || (cost.characters != 0 && copies > remainingCharacters / cost.characters)) {
        errorOnce(
            context.diagnostics,
            "odf.model_expansion_limit_exceeded",
            "Repeated OpenDocument rows or cells exceed the expanded-model safety limit.",
            context.source);
        context.expansionFailed = true;
        return false;
    }
    context.expandedItems += cost.items * copies;
    context.expandedCharacters += cost.characters * copies;
    return true;
}

struct ListIdentityAttributes {
    std::string explicitStyleName;
    std::string continueList;
    std::string elementId;
    bool continueNumbering{false};
};

ListIdentityAttributes listIdentityAttributes(const QXmlStreamReader& xml)
{
    ListIdentityAttributes result;
    result.explicitStyleName = toUtf8(attribute(
        xml, textNamespace(), QStringLiteral("style-name")));
    result.continueList = toUtf8(attribute(
        xml, textNamespace(), QStringLiteral("continue-list")));
    if (result.continueList.starts_with('#')) {
        result.continueList.erase(0, 1);
    }
    result.elementId = toUtf8(attribute(
        xml, xmlNamespace(), QStringLiteral("id")));
    const auto continueNumbering = attribute(
        xml, textNamespace(), QStringLiteral("continue-numbering"));
    result.continueNumbering =
        continueNumbering.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0
        || continueNumbering == QStringLiteral("1");
    return result;
}

int finalizeTopLevelListIdentifier(
    const ListIdentityAttributes& attributes,
    const std::string& effectiveStyleName,
    int provisionalIdentifier,
    ReadContext& context)
{
    std::optional<int> identifier;
    if (!attributes.continueList.empty()) {
        const auto found = context.listElementIdentifiers.find(
            attributes.continueList);
        if (found != context.listElementIdentifiers.end()) {
            identifier = found->second;
        } else {
            warningOnce(
                context.diagnostics,
                "odf.list_continuation_target_missing",
                "An OpenDocument list continuation target could not be resolved.",
                context.source);
        }
    }
    if (attributes.continueList.empty() && attributes.continueNumbering) {
        if (context.lastTopLevelListIdentifier
            && context.lastTopLevelListStyleName
            && *context.lastTopLevelListStyleName == effectiveStyleName) {
            identifier = context.lastTopLevelListIdentifier;
        } else {
            warningOnce(
                context.diagnostics,
                "odf.list_continuation_style_mismatch",
                "OpenDocument continue-numbering was ignored because the previous list style differs.",
                context.source);
        }
    }
    if (!identifier) {
        identifier = provisionalIdentifier;
    }
    if (!attributes.elementId.empty()) {
        const auto [existing, inserted] = context.listElementIdentifiers.emplace(
            attributes.elementId, *identifier);
        if (!inserted && existing->second != *identifier) {
            warningOnce(
                context.diagnostics,
                "odf.duplicate_list_xml_id",
                "Duplicate OpenDocument list XML identifiers were not merged.",
                context.source);
        }
    }
    context.lastTopLevelListIdentifier = identifier;
    context.lastTopLevelListStyleName = effectiveStyleName;
    return *identifier;
}

void parseListParagraphs(
    QXmlStreamReader& xml,
    std::vector<WordParagraph>& paragraphs,
    ReadContext& context,
    int level,
    std::optional<int> inheritedIdentifier = std::nullopt,
    std::uint32_t semanticDepth = 0)
{
    if (!allowNesting(context, semanticDepth)) {
        abortExpansion(xml, context);
        return;
    }
    const auto identity = listIdentityAttributes(xml);
    const bool topLevel = !inheritedIdentifier.has_value();
    const int identifier = inheritedIdentifier
        ? *inheritedIdentifier : context.nextListIdentifier++;
    const auto firstListParagraph = paragraphs.size();
    std::optional<std::string> inferredStyleName;
    const auto safeLevel = std::clamp(level, 0, 8);
    if (safeLevel != level) {
        warningOnce(
            context.diagnostics,
            "odf.list_depth_clamped",
            "OpenDocument lists deeper than nine levels are flattened to level eight.",
            context.source);
    }

    while (xml.readNextStartElement()) {
        const bool listItem = isElement(
            xml, textNamespace(), QStringLiteral("list-item"));
        const bool listHeader = isElement(
            xml, textNamespace(), QStringLiteral("list-header"));
        if (!listItem && !listHeader) {
            xml.skipCurrentElement();
            continue;
        }
        bool hasNumberedParagraph = false;
        while (xml.readNextStartElement()) {
            if (isElement(xml, textNamespace(), QStringLiteral("p"))
                || isElement(xml, textNamespace(), QStringLiteral("h"))) {
                std::string paragraphStyleName;
                auto paragraph = parseParagraph(
                    xml, context, &paragraphStyleName);
                if (context.expansionFailed) {
                    return;
                }
                if (listItem) {
                    if (!inferredStyleName
                        && identity.explicitStyleName.empty()) {
                        inferredStyleName = resolveStyle(
                            "paragraph", paragraphStyleName, context.styles,
                            context.source, context.diagnostics)
                                                .listStyleName.value_or(
                                                    std::string{});
                    }
                    paragraph.properties.numberingId = identifier;
                    paragraph.properties.numberingLevel = safeLevel;
                    paragraph.properties.numberingContinuation = hasNumberedParagraph;
                    hasNumberedParagraph = true;
                }
                paragraphs.push_back(std::move(paragraph));
            } else if (isElement(xml, textNamespace(), QStringLiteral("list"))) {
                parseListParagraphs(
                    xml, paragraphs, context, level + 1, identifier,
                    semanticDepth + 1);
                if (context.expansionFailed) {
                    return;
                }
            } else {
                warningOnce(
                    context.diagnostics,
                    "odf.unsupported_list_content_omitted",
                    "An unsupported OpenDocument list item was omitted.",
                    context.source);
                xml.skipCurrentElement();
            }
        }
    }
    if (!topLevel) {
        return;
    }

    const auto effectiveStyleName = identity.explicitStyleName.empty()
        ? inferredStyleName.value_or(std::string{})
        : identity.explicitStyleName;
    const auto finalIdentifier = finalizeTopLevelListIdentifier(
        identity, effectiveStyleName, identifier, context);
    if (finalIdentifier == identifier) {
        return;
    }
    for (auto index = firstListParagraph; index < paragraphs.size(); ++index) {
        if (paragraphs[index].properties.numberingId == identifier) {
            paragraphs[index].properties.numberingId = finalIdentifier;
        }
    }
}

WordTableCell parseTableCell(QXmlStreamReader& xml, ReadContext& context)
{
    WordTableCell cell;
    if (!consumeItems(context, 1)) {
        abortExpansion(xml, context);
        return cell;
    }
    const auto columnsSpanned = attribute(
        xml, tableNamespace(), QStringLiteral("number-columns-spanned"));
    if (!columnsSpanned.isEmpty() && columnsSpanned != QStringLiteral("1")) {
        warningOnce(
            context.diagnostics,
            "odf.merged_cells_flattened",
            "Merged OpenDocument table cells are flattened to one editable cell.",
            context.source);
    }
    while (xml.readNextStartElement()) {
        if (isElement(xml, textNamespace(), QStringLiteral("p"))
            || isElement(xml, textNamespace(), QStringLiteral("h"))) {
            auto paragraph = parseParagraph(xml, context);
            if (context.expansionFailed) {
                return cell;
            }
            cell.paragraphs.push_back(std::move(paragraph));
        } else if (isElement(xml, textNamespace(), QStringLiteral("list"))) {
            parseListParagraphs(xml, cell.paragraphs, context, 0);
            if (context.expansionFailed) {
                return cell;
            }
        } else if (isElement(xml, tableNamespace(), QStringLiteral("table"))) {
            warningOnce(
                context.diagnostics,
                "odf.nested_table_omitted",
                "Nested OpenDocument tables are not first-class flow items yet.",
                context.source);
            xml.skipCurrentElement();
        } else {
            xml.skipCurrentElement();
        }
    }
    if (cell.paragraphs.empty()) {
        if (consumeItems(context, 1)) {
            cell.paragraphs.emplace_back();
        }
    }
    return cell;
}

WordTableRow parseTableRow(QXmlStreamReader& xml, ReadContext& context)
{
    WordTableRow row;
    if (!consumeItems(context, 1)) {
        abortExpansion(xml, context);
        return row;
    }
    while (xml.readNextStartElement()) {
        if (isElement(xml, tableNamespace(), QStringLiteral("table-cell"))) {
            const auto repeat = repeatedCount(
                xml, QStringLiteral("number-columns-repeated"), context);
            if (context.expansionFailed) {
                abortExpansion(xml, context);
                return row;
            }
            const auto cell = parseTableCell(xml, context);
            if (context.expansionFailed
                || !consumeCopies(context, modelCost(cell), repeat - 1)) {
                abortExpansion(xml, context);
                return row;
            }
            for (std::uint64_t index = 0; index < repeat; ++index) {
                row.cells.push_back(cell);
            }
        } else if (isElement(xml, tableNamespace(), QStringLiteral("covered-table-cell"))) {
            const auto repeat = repeatedCount(
                xml, QStringLiteral("number-columns-repeated"), context);
            if (context.expansionFailed) {
                abortExpansion(xml, context);
                return row;
            }
            xml.skipCurrentElement();
            const ExpansionCost emptyCellCost{2, 0};
            if (!consumeCopies(context, emptyCellCost, repeat)) {
                abortExpansion(xml, context);
                return row;
            }
            for (std::uint64_t index = 0; index < repeat; ++index) {
                WordTableCell cell;
                cell.paragraphs.emplace_back();
                row.cells.push_back(std::move(cell));
            }
        } else {
            xml.skipCurrentElement();
        }
    }
    return row;
}

WordTable parseTable(QXmlStreamReader& xml, ReadContext& context)
{
    WordTable table;
    if (!consumeItems(context, 1)) {
        abortExpansion(xml, context);
        return table;
    }
    while (xml.readNextStartElement()) {
        if (isElement(xml, tableNamespace(), QStringLiteral("table-row"))) {
            const auto repeat = repeatedCount(
                xml, QStringLiteral("number-rows-repeated"), context);
            if (context.expansionFailed) {
                abortExpansion(xml, context);
                return table;
            }
            const auto row = parseTableRow(xml, context);
            if (context.expansionFailed
                || !consumeCopies(context, modelCost(row), repeat - 1)) {
                abortExpansion(xml, context);
                return table;
            }
            for (std::uint64_t index = 0; index < repeat; ++index) {
                table.rows.push_back(row);
            }
        } else {
            xml.skipCurrentElement();
        }
    }
    return table;
}

void parseBlockContainer(
    QXmlStreamReader& xml,
    WordDocument& document,
    ReadContext& context,
    std::uint32_t semanticDepth = 0)
{
    if (!allowNesting(context, semanticDepth)) {
        abortExpansion(xml, context);
        return;
    }
    while (xml.readNextStartElement()) {
        if (isElement(xml, textNamespace(), QStringLiteral("p"))
            || isElement(xml, textNamespace(), QStringLiteral("h"))) {
            auto paragraph = parseParagraph(xml, context);
            if (context.expansionFailed) {
                return;
            }
            document.appendParagraph(std::move(paragraph));
        } else if (isElement(xml, textNamespace(), QStringLiteral("list"))) {
            std::vector<WordParagraph> paragraphs;
            parseListParagraphs(xml, paragraphs, context, 0);
            if (context.expansionFailed) {
                return;
            }
            for (auto& paragraph : paragraphs) {
                document.appendParagraph(std::move(paragraph));
            }
        } else if (isElement(xml, tableNamespace(), QStringLiteral("table"))) {
            auto table = parseTable(xml, context);
            if (context.expansionFailed) {
                return;
            }
            document.appendTable(std::move(table));
        } else if (isElement(xml, textNamespace(), QStringLiteral("section"))) {
            parseBlockContainer(xml, document, context, semanticDepth + 1);
            if (context.expansionFailed) {
                return;
            }
        } else if (isElement(xml, textNamespace(), QStringLiteral("tracked-changes"))) {
            warningOnce(
                context.diagnostics,
                "odf.tracked_changes_omitted",
                "Tracked changes are not first-class and were omitted.",
                context.source);
            xml.skipCurrentElement();
        } else {
            warningOnce(
                context.diagnostics,
                "odf.unsupported_body_block_omitted",
                "An unsupported OpenDocument body block was omitted.",
                context.source);
            xml.skipCurrentElement();
        }
    }
}

void parseBody(
    const QByteArray& bytes,
    const StyleMap& styles,
    WordDocument& document,
    const std::filesystem::path& source,
    std::vector<Diagnostic>& diagnostics,
    std::uint64_t maximumExpandedCharacters)
{
    QXmlStreamReader xml(bytes);
    bool foundTextBody = false;
    while (!xml.atEnd()) {
        xml.readNext();
        if (!isElement(xml, officeNamespace(), QStringLiteral("body"))) {
            continue;
        }
        while (xml.readNextStartElement()) {
            if (isElement(xml, officeNamespace(), QStringLiteral("text"))) {
                foundTextBody = true;
                ReadContext context{
                    styles, source, diagnostics, {}, std::nullopt, std::nullopt,
                    1, 0, 0,
                    maximumExpandedCharacters, false};
                parseBlockContainer(xml, document, context);
            } else {
                xml.skipCurrentElement();
            }
        }
        break;
    }
    if (!foundTextBody) {
        diagnostics.push_back(diagnostic(
            DiagnosticSeverity::error,
            "odf.body_missing",
            "The OpenDocument text XML has no office:text body.",
            source));
    }
}

void parseMetadata(
    const QByteArray& bytes,
    WordDocument& document,
    const std::filesystem::path& source,
    std::vector<Diagnostic>& diagnostics)
{
    QXmlStreamReader xml(bytes);
    while (!xml.atEnd()) {
        xml.readNext();
        if (!isElement(xml, officeNamespace(), QStringLiteral("meta"))) {
            continue;
        }
        while (xml.readNextStartElement()) {
            const auto namespaceUri = xml.namespaceUri().toString();
            const auto name = xml.name().toString();
            std::string key;
            if (namespaceUri == dcNamespace()) {
                if (name == QStringLiteral("title")) key = "Title";
                else if (name == QStringLiteral("subject")) key = "Subject";
                else if (name == QStringLiteral("description")) key = "Description";
                else if (name == QStringLiteral("creator")) key = "LastModifiedBy";
                else if (name == QStringLiteral("date")) key = "Modified";
            } else if (namespaceUri == metaNamespace()) {
                if (name == QStringLiteral("initial-creator")) key = "Author";
                else if (name == QStringLiteral("creation-date")) key = "Created";
                else if (name == QStringLiteral("keyword")) key = "Keywords";
                else if (name == QStringLiteral("generator")) key = "Application";
            }
            if (key.empty()) {
                xml.skipCurrentElement();
                continue;
            }
            const auto value = toUtf8(xml.readElementText());
            if (key == "Keywords" && document.metadata().contains(key)
                && !document.metadata()[key].empty()) {
                document.metadata()[key] += "; " + value;
            } else {
                document.metadata()[key] = value;
            }
        }
        break;
    }
    if (!document.metadata().contains("Author")
        && document.metadata().contains("LastModifiedBy")) {
        document.metadata()["Author"] = document.metadata()["LastModifiedBy"];
    }
    if (xml.hasError()) {
        diagnostics.push_back(diagnostic(
            DiagnosticSeverity::warning,
            "odf.metadata_xml_error",
            "OpenDocument metadata could not be read completely: "
                + toUtf8(xml.errorString()),
            source));
    }
}

bool validateManifest(
    const QByteArray& bytes,
    const std::filesystem::path& source,
    std::vector<Diagnostic>& diagnostics,
    std::set<std::string>& declaredFiles)
{
    QXmlStreamReader xml(bytes);
    bool foundRoot = false;
    int packageRootEntryCount = 0;
    std::uint64_t manifestEntryCount = 0;
    bool encrypted = false;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.tokenType() == QXmlStreamReader::DTD) {
            diagnostics.push_back(diagnostic(
                DiagnosticSeverity::error,
                "odf.dtd_not_allowed",
                "The OpenDocument manifest must not contain a DTD.",
                source));
            return false;
        }
        if (!xml.isStartElement()) {
            continue;
        }
        if (!foundRoot) {
            foundRoot = true;
            if (!isElement(xml, manifestNamespace(), QStringLiteral("manifest"))) {
                diagnostics.push_back(diagnostic(
                    DiagnosticSeverity::error,
                    "odf.invalid_manifest_root",
                    "The ODT manifest has an invalid root element.",
                    source));
                return false;
            }
            continue;
        }
        if (isElement(xml, manifestNamespace(), QStringLiteral("file-entry"))) {
            ++manifestEntryCount;
            if (manifestEntryCount > static_cast<std::uint64_t>(maximumPackageEntries)) {
                diagnostics.push_back(diagnostic(
                    DiagnosticSeverity::error,
                    "odf.too_many_manifest_entries",
                    "The ODT manifest contains too many file entries.",
                    source));
                return false;
            }
            const auto fullPath = toUtf8(attribute(
                xml, manifestNamespace(), QStringLiteral("full-path")));
            const auto mediaType = toUtf8(attribute(
                xml, manifestNamespace(), QStringLiteral("media-type")));
            if (fullPath == "/") {
                ++packageRootEntryCount;
                if (packageRootEntryCount > 1) {
                    diagnostics.push_back(diagnostic(
                        DiagnosticSeverity::error,
                        "odf.duplicate_manifest_root_entry",
                        "The ODT manifest declares the package root more than once.",
                        source));
                }
                if (mediaType != odtMimeType) {
                    diagnostics.push_back(diagnostic(
                        DiagnosticSeverity::error,
                        "odf.manifest_mimetype_mismatch",
                        "The ODT manifest root media type does not match the mimetype entry.",
                        source));
                }
            } else if (!fullPath.empty()) {
                bool pathValid = true;
                if (fullPath.size() > maximumPackageEntryNameBytes) {
                    diagnostics.push_back(diagnostic(
                        DiagnosticSeverity::error,
                        "odf.manifest_path_too_long",
                        "The ODT manifest contains a path longer than the safety limit.",
                        source));
                    pathValid = false;
                } else if (!safePackagePath(fullPath)) {
                    diagnostics.push_back(diagnostic(
                        DiagnosticSeverity::error,
                        "odf.unsafe_manifest_path",
                        "The ODT manifest contains an unsafe file path: " + fullPath + ".",
                        source));
                    pathValid = false;
                }
                if (pathValid && !declaredFiles.insert(fullPath).second) {
                    diagnostics.push_back(diagnostic(
                        DiagnosticSeverity::error,
                        "odf.duplicate_manifest_entry",
                        "The ODT manifest declares a file more than once: " + fullPath + ".",
                        source));
                }
            }
        } else if (isElement(xml, manifestNamespace(), QStringLiteral("encryption-data"))) {
            encrypted = true;
        }
    }
    if (xml.hasError()) {
        diagnostics.push_back(diagnostic(
            DiagnosticSeverity::error,
            "odf.manifest_xml_error",
            "The ODT manifest is malformed: " + toUtf8(xml.errorString()),
            source));
    }
    if (!foundRoot || packageRootEntryCount == 0) {
        diagnostics.push_back(diagnostic(
            DiagnosticSeverity::error,
            "odf.manifest_root_entry_missing",
            "The ODT manifest does not declare the package root media type.",
            source));
    }
    if (encrypted) {
        diagnostics.push_back(diagnostic(
            DiagnosticSeverity::error,
            "odf.encryption_unsupported",
            "Encrypted OpenDocument packages are not supported.",
            source));
    }
    return !ii::document::hasErrors(diagnostics);
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
            "odf.invalid_numbering_id",
            "OpenDocument numbering identifiers must be non-negative.",
            destination));
    }
    if (paragraph.properties.numberingContinuation
        && !paragraph.properties.numberingId) {
        diagnostics.push_back(diagnostic(
            DiagnosticSeverity::error,
            "odf.invalid_numbering_continuation",
            "An OpenDocument list-item continuation requires a numbering identifier.",
            destination));
    }
    if (paragraph.properties.numberingLevel < 0
        || paragraph.properties.numberingLevel > 8) {
        diagnostics.push_back(diagnostic(
            DiagnosticSeverity::error,
            "odf.invalid_numbering_level",
            "OpenDocument numbering levels must be between 0 and 8.",
            destination));
    }
    for (const auto& run : paragraph.runs) {
        if (!std::isfinite(run.properties.fontSizePoints)
            || run.properties.fontSizePoints < 0.0) {
            diagnostics.push_back(diagnostic(
                DiagnosticSeverity::error,
                "odf.invalid_font_size",
                "OpenDocument run font sizes must be finite and non-negative.",
                destination));
        }
        if (!run.properties.color.empty() && !isHexColor(run.properties.color)) {
            diagnostics.push_back(diagnostic(
                DiagnosticSeverity::error,
                "odf.invalid_color",
                "OpenDocument run colors must contain exactly six hexadecimal digits.",
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
                    "odf.invalid_numbering_continuation",
                    "An OpenDocument list-item continuation has no preceding item at its level.",
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
                "odf.empty_table",
                "OpenDocument tables must contain at least one row.",
                destination));
            continue;
        }
        for (const auto& row : table.rows) {
            if (row.cells.empty()) {
                diagnostics.push_back(diagnostic(
                    DiagnosticSeverity::error,
                    "odf.empty_table_row",
                    "OpenDocument table rows must contain at least one cell.",
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

    const auto& section = document.section();
    if (section.pageWidthTwips <= 0 || section.pageHeightTwips <= 0
        || section.marginTopTwips < 0 || section.marginRightTwips < 0
        || section.marginBottomTwips < 0 || section.marginLeftTwips < 0
        || section.marginLeftTwips + section.marginRightTwips >= section.pageWidthTwips
        || section.marginTopTwips + section.marginBottomTwips >= section.pageHeightTwips) {
        diagnostics.push_back(diagnostic(
            DiagnosticSeverity::error,
            "odf.invalid_section_geometry",
            "OpenDocument page dimensions and margins must describe a positive content area.",
            destination));
    }
}

bool hasRunProperties(const WordRunProperties& properties)
{
    return properties.bold || properties.italic || properties.underline
        || !properties.fontFamily.empty() || !properties.eastAsiaFontFamily.empty()
        || properties.fontSizePoints > 0.0 || !properties.color.empty();
}

using RunStyleKey = std::tuple<
    bool,
    bool,
    bool,
    std::string,
    std::string,
    double,
    std::string>;

RunStyleKey runStyleKey(const WordRunProperties& properties)
{
    return {
        properties.bold,
        properties.italic,
        properties.underline,
        properties.fontFamily,
        properties.eastAsiaFontFamily,
        properties.fontSizePoints,
        properties.color};
}

using ParagraphStyleKey = std::pair<std::string, WordParagraphAlignment>;

ParagraphStyleKey paragraphStyleKey(const WordParagraphProperties& properties)
{
    return {properties.styleId, properties.alignment};
}

struct RunStyleRecord {
    std::string name;
    WordRunProperties properties;
};

struct ParagraphStyleRecord {
    std::string name;
    std::string parent;
    WordParagraphAlignment alignment{WordParagraphAlignment::automatic};
};

struct WritePlan {
    std::map<RunStyleKey, std::string> runStyleNames;
    std::map<ParagraphStyleKey, std::string> paragraphStyleNames;
    std::vector<RunStyleRecord> runStyles;
    std::vector<ParagraphStyleRecord> paragraphStyles;
    std::set<std::string> namedParagraphStyles;
    std::set<std::string> fonts;
    std::set<int> numberingIdentifiers;
};

void collectParagraph(const WordParagraph& paragraph, WritePlan& plan)
{
    if (!paragraph.properties.styleId.empty()) {
        plan.namedParagraphStyles.insert(paragraph.properties.styleId);
    }
    if (paragraph.properties.alignment != WordParagraphAlignment::automatic) {
        const auto key = paragraphStyleKey(paragraph.properties);
        if (!plan.paragraphStyleNames.contains(key)) {
            const auto name = "IGDP" + std::to_string(plan.paragraphStyles.size() + 1);
            plan.paragraphStyleNames[key] = name;
            plan.paragraphStyles.push_back(
                {name, paragraph.properties.styleId, paragraph.properties.alignment});
        }
    }
    if (paragraph.properties.numberingId) {
        plan.numberingIdentifiers.insert(*paragraph.properties.numberingId);
    }
    for (const auto& run : paragraph.runs) {
        if (!run.properties.fontFamily.empty()) {
            plan.fonts.insert(run.properties.fontFamily);
        }
        if (!run.properties.eastAsiaFontFamily.empty()) {
            plan.fonts.insert(run.properties.eastAsiaFontFamily);
        }
        if (!hasRunProperties(run.properties)) {
            continue;
        }
        const auto key = runStyleKey(run.properties);
        if (!plan.runStyleNames.contains(key)) {
            const auto name = "IGDT" + std::to_string(plan.runStyles.size() + 1);
            plan.runStyleNames[key] = name;
            plan.runStyles.push_back({name, run.properties});
        }
    }
}

WritePlan makeWritePlan(const WordDocument& document)
{
    WritePlan plan;
    for (const auto& block : document.blocks()) {
        if (const auto* paragraph = std::get_if<WordParagraph>(&block)) {
            collectParagraph(*paragraph, plan);
            continue;
        }
        for (const auto& row : std::get<WordTable>(block).rows) {
            for (const auto& cell : row.cells) {
                for (const auto& paragraph : cell.paragraphs) {
                    collectParagraph(paragraph, plan);
                }
            }
        }
    }
    return plan;
}

std::string paragraphStyleName(
    const WordParagraph& paragraph,
    const WritePlan& plan)
{
    if (paragraph.properties.alignment != WordParagraphAlignment::automatic) {
        const auto found = plan.paragraphStyleNames.find(
            paragraphStyleKey(paragraph.properties));
        if (found != plan.paragraphStyleNames.end()) {
            return found->second;
        }
    }
    return paragraph.properties.styleId.empty() ? "IGDBase"
                                                 : paragraph.properties.styleId;
}

std::string runStyleName(const WordRun& run, const WritePlan& plan)
{
    if (!hasRunProperties(run.properties)) {
        return {};
    }
    const auto found = plan.runStyleNames.find(runStyleKey(run.properties));
    return found == plan.runStyleNames.end() ? std::string{} : found->second;
}

std::optional<int> headingLevel(const std::string& styleId)
{
    constexpr std::string_view prefix = "Heading";
    if (!styleId.starts_with(prefix)) {
        return std::nullopt;
    }
    const auto digits = styleId.substr(prefix.size());
    if (digits.empty()
        || std::ranges::any_of(digits, [](unsigned char character) {
               return std::isdigit(character) == 0;
           })) {
        return std::nullopt;
    }
    try {
        const auto level = std::stoi(digits);
        if (level >= 1 && level <= 9) {
            return level;
        }
    } catch (const std::exception&) {
    }
    return std::nullopt;
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

void writeCommonNamespaces(QXmlStreamWriter& xml)
{
    xml.writeNamespace(officeNamespace(), QStringLiteral("office"));
    xml.writeNamespace(textNamespace(), QStringLiteral("text"));
    xml.writeNamespace(styleNamespace(), QStringLiteral("style"));
    xml.writeNamespace(tableNamespace(), QStringLiteral("table"));
    xml.writeNamespace(foNamespace(), QStringLiteral("fo"));
    xml.writeNamespace(svgNamespace(), QStringLiteral("svg"));
    xml.writeNamespace(dcNamespace(), QStringLiteral("dc"));
    xml.writeNamespace(metaNamespace(), QStringLiteral("meta"));
}

QString pointText(double points)
{
    auto value = QString::number(points, 'f', 4);
    while (value.contains(QLatin1Char('.')) && value.endsWith(QLatin1Char('0'))) {
        value.chop(1);
    }
    if (value.endsWith(QLatin1Char('.'))) {
        value.chop(1);
    }
    return value + QStringLiteral("pt");
}

QString twipsText(int twips)
{
    return pointText(static_cast<double>(twips) / 20.0);
}

void writeFontFaces(QXmlStreamWriter& xml, const WritePlan& plan)
{
    xml.writeStartElement(officeNamespace(), QStringLiteral("font-face-decls"));
    for (const auto& font : plan.fonts) {
        xml.writeStartElement(styleNamespace(), QStringLiteral("font-face"));
        xml.writeAttribute(styleNamespace(), QStringLiteral("name"), fromUtf8(font));
        xml.writeAttribute(svgNamespace(), QStringLiteral("font-family"),
                           QLatin1Char('\'') + fromUtf8(font) + QLatin1Char('\''));
        xml.writeEndElement();
    }
    xml.writeEndElement();
}

void writeNamedStylesAndLists(QXmlStreamWriter& xml, const WritePlan& plan)
{
    xml.writeStartElement(officeNamespace(), QStringLiteral("styles"));
    xml.writeStartElement(styleNamespace(), QStringLiteral("style"));
    xml.writeAttribute(styleNamespace(), QStringLiteral("name"), QStringLiteral("IGDBase"));
    xml.writeAttribute(styleNamespace(), QStringLiteral("family"), QStringLiteral("paragraph"));
    xml.writeAttribute(styleNamespace(), QStringLiteral("master-page-name"), QStringLiteral("IGDStandard"));
    xml.writeEndElement();
    for (const auto& styleId : plan.namedParagraphStyles) {
        xml.writeStartElement(styleNamespace(), QStringLiteral("style"));
        xml.writeAttribute(styleNamespace(), QStringLiteral("name"), fromUtf8(styleId));
        xml.writeAttribute(styleNamespace(), QStringLiteral("display-name"), fromUtf8(styleId));
        xml.writeAttribute(styleNamespace(), QStringLiteral("family"), QStringLiteral("paragraph"));
        xml.writeAttribute(styleNamespace(), QStringLiteral("master-page-name"), QStringLiteral("IGDStandard"));
        if (const auto level = headingLevel(styleId)) {
            xml.writeAttribute(styleNamespace(), QStringLiteral("default-outline-level"),
                               QString::number(*level));
        }
        xml.writeEndElement();
    }
    for (const auto identifier : plan.numberingIdentifiers) {
        xml.writeStartElement(textNamespace(), QStringLiteral("list-style"));
        xml.writeAttribute(styleNamespace(), QStringLiteral("name"),
                           QStringLiteral("IGDL") + QString::number(identifier));
        for (int level = 1; level <= 9; ++level) {
            xml.writeStartElement(textNamespace(), QStringLiteral("list-level-style-number"));
            xml.writeAttribute(textNamespace(), QStringLiteral("level"), QString::number(level));
            xml.writeAttribute(styleNamespace(), QStringLiteral("num-format"), QStringLiteral("1"));
            xml.writeStartElement(styleNamespace(), QStringLiteral("list-level-properties"));
            xml.writeAttribute(textNamespace(), QStringLiteral("space-before"),
                               pointText(static_cast<double>(level) * 18.0));
            xml.writeAttribute(textNamespace(), QStringLiteral("min-label-width"),
                               QStringLiteral("18pt"));
            xml.writeEndElement();
            xml.writeEndElement();
        }
        xml.writeEndElement();
    }
    xml.writeEndElement();
}

void writeRunStyle(QXmlStreamWriter& xml, const RunStyleRecord& record)
{
    xml.writeStartElement(styleNamespace(), QStringLiteral("style"));
    xml.writeAttribute(styleNamespace(), QStringLiteral("name"), fromUtf8(record.name));
    xml.writeAttribute(styleNamespace(), QStringLiteral("family"), QStringLiteral("text"));
    xml.writeStartElement(styleNamespace(), QStringLiteral("text-properties"));
    const auto& properties = record.properties;
    if (properties.bold) {
        xml.writeAttribute(foNamespace(), QStringLiteral("font-weight"), QStringLiteral("bold"));
        xml.writeAttribute(styleNamespace(), QStringLiteral("font-weight-asian"), QStringLiteral("bold"));
    }
    if (properties.italic) {
        xml.writeAttribute(foNamespace(), QStringLiteral("font-style"), QStringLiteral("italic"));
        xml.writeAttribute(styleNamespace(), QStringLiteral("font-style-asian"), QStringLiteral("italic"));
    }
    if (properties.underline) {
        xml.writeAttribute(styleNamespace(), QStringLiteral("text-underline-style"), QStringLiteral("solid"));
        xml.writeAttribute(styleNamespace(), QStringLiteral("text-underline-width"), QStringLiteral("auto"));
        xml.writeAttribute(styleNamespace(), QStringLiteral("text-underline-color"), QStringLiteral("font-color"));
    }
    if (!properties.fontFamily.empty()) {
        xml.writeAttribute(styleNamespace(), QStringLiteral("font-name"), fromUtf8(properties.fontFamily));
        xml.writeAttribute(foNamespace(), QStringLiteral("font-family"),
                           QLatin1Char('\'') + fromUtf8(properties.fontFamily) + QLatin1Char('\''));
    }
    if (!properties.eastAsiaFontFamily.empty()) {
        xml.writeAttribute(styleNamespace(), QStringLiteral("font-name-asian"),
                           fromUtf8(properties.eastAsiaFontFamily));
    }
    if (properties.fontSizePoints > 0.0) {
        xml.writeAttribute(foNamespace(), QStringLiteral("font-size"),
                           pointText(properties.fontSizePoints));
        xml.writeAttribute(styleNamespace(), QStringLiteral("font-size-asian"),
                           pointText(properties.fontSizePoints));
    }
    if (!properties.color.empty()) {
        xml.writeAttribute(foNamespace(), QStringLiteral("color"),
                           QLatin1Char('#') + fromUtf8(properties.color));
    }
    xml.writeEndElement();
    xml.writeEndElement();
}

QString alignmentValue(WordParagraphAlignment alignment)
{
    switch (alignment) {
    case WordParagraphAlignment::left:
        return QStringLiteral("left");
    case WordParagraphAlignment::center:
        return QStringLiteral("center");
    case WordParagraphAlignment::right:
        return QStringLiteral("right");
    case WordParagraphAlignment::justified:
        return QStringLiteral("justify");
    case WordParagraphAlignment::automatic:
        return {};
    }
    return {};
}

void writeAutomaticStyles(
    QXmlStreamWriter& xml,
    const WritePlan& plan,
    const WordSectionProperties& section,
    bool includeTextStyles,
    bool includePageLayout)
{
    xml.writeStartElement(officeNamespace(), QStringLiteral("automatic-styles"));
    if (includeTextStyles) {
        for (const auto& record : plan.paragraphStyles) {
            xml.writeStartElement(styleNamespace(), QStringLiteral("style"));
            xml.writeAttribute(styleNamespace(), QStringLiteral("name"), fromUtf8(record.name));
            xml.writeAttribute(styleNamespace(), QStringLiteral("family"), QStringLiteral("paragraph"));
            xml.writeAttribute(styleNamespace(), QStringLiteral("master-page-name"), QStringLiteral("IGDStandard"));
            if (!record.parent.empty()) {
                xml.writeAttribute(styleNamespace(), QStringLiteral("parent-style-name"),
                                   fromUtf8(record.parent));
            }
            xml.writeStartElement(styleNamespace(), QStringLiteral("paragraph-properties"));
            xml.writeAttribute(foNamespace(), QStringLiteral("text-align"),
                               alignmentValue(record.alignment));
            xml.writeEndElement();
            xml.writeEndElement();
        }
        for (const auto& record : plan.runStyles) {
            writeRunStyle(xml, record);
        }
    }
    if (includePageLayout) {
        xml.writeStartElement(styleNamespace(), QStringLiteral("page-layout"));
        xml.writeAttribute(styleNamespace(), QStringLiteral("name"), QStringLiteral("IGDPageLayout"));
        xml.writeStartElement(styleNamespace(), QStringLiteral("page-layout-properties"));
        xml.writeAttribute(foNamespace(), QStringLiteral("page-width"), twipsText(section.pageWidthTwips));
        xml.writeAttribute(foNamespace(), QStringLiteral("page-height"), twipsText(section.pageHeightTwips));
        xml.writeAttribute(foNamespace(), QStringLiteral("margin-top"), twipsText(section.marginTopTwips));
        xml.writeAttribute(foNamespace(), QStringLiteral("margin-right"), twipsText(section.marginRightTwips));
        xml.writeAttribute(foNamespace(), QStringLiteral("margin-bottom"), twipsText(section.marginBottomTwips));
        xml.writeAttribute(foNamespace(), QStringLiteral("margin-left"), twipsText(section.marginLeftTwips));
        xml.writeEndElement();
        xml.writeEndElement();
    }
    xml.writeEndElement();
}

void writeMasterStyles(QXmlStreamWriter& xml)
{
    xml.writeStartElement(officeNamespace(), QStringLiteral("master-styles"));
    xml.writeStartElement(styleNamespace(), QStringLiteral("master-page"));
    xml.writeAttribute(styleNamespace(), QStringLiteral("name"), QStringLiteral("IGDStandard"));
    xml.writeAttribute(styleNamespace(), QStringLiteral("page-layout-name"), QStringLiteral("IGDPageLayout"));
    xml.writeEndElement();
    xml.writeEndElement();
}

void writeOdfText(QXmlStreamWriter& xml, const std::string& text)
{
    const auto value = fromUtf8(text);
    QString chunk;
    auto flush = [&]() {
        if (!chunk.isEmpty()) {
            xml.writeCharacters(chunk);
            chunk.clear();
        }
    };
    for (qsizetype index = 0; index < value.size(); ++index) {
        const auto character = value[index];
        if (character == QLatin1Char(' ')) {
            flush();
            qsizetype count = 1;
            while (index + 1 < value.size() && value[index + 1] == QLatin1Char(' ')) {
                ++index;
                ++count;
            }
            xml.writeStartElement(textNamespace(), QStringLiteral("s"));
            if (count > 1) {
                xml.writeAttribute(textNamespace(), QStringLiteral("c"), QString::number(count));
            }
            xml.writeEndElement();
        } else if (character == QLatin1Char('\t')) {
            flush();
            xml.writeEmptyElement(textNamespace(), QStringLiteral("tab"));
        } else if (character == QLatin1Char('\n') || character == QLatin1Char('\r')) {
            flush();
            xml.writeEmptyElement(textNamespace(), QStringLiteral("line-break"));
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

void writeParagraph(
    QXmlStreamWriter& xml,
    const WordParagraph& paragraph,
    const WritePlan& plan)
{
    const auto level = headingLevel(paragraph.properties.styleId);
    xml.writeStartElement(
        textNamespace(), level ? QStringLiteral("h") : QStringLiteral("p"));
    const auto styleName = paragraphStyleName(paragraph, plan);
    if (!styleName.empty()) {
        xml.writeAttribute(textNamespace(), QStringLiteral("style-name"), fromUtf8(styleName));
    }
    if (level) {
        xml.writeAttribute(textNamespace(), QStringLiteral("outline-level"),
                           QString::number(*level));
    }
    for (const auto& run : paragraph.runs) {
        xml.writeStartElement(textNamespace(), QStringLiteral("span"));
        const auto style = runStyleName(run, plan);
        if (!style.empty()) {
            xml.writeAttribute(textNamespace(), QStringLiteral("style-name"), fromUtf8(style));
        }
        writeOdfText(xml, run.text);
        xml.writeEndElement();
    }
    xml.writeEndElement();
}

void writeListLevel(
    QXmlStreamWriter& xml,
    const std::vector<const WordParagraph*>& paragraphs,
    std::size_t& index,
    int level,
    const WritePlan& plan)
{
    if (index >= paragraphs.size()) {
        return;
    }
    xml.writeStartElement(textNamespace(), QStringLiteral("list"));
    xml.writeAttribute(
        textNamespace(), QStringLiteral("style-name"),
        QStringLiteral("IGDL")
            + QString::number(*paragraphs.front()->properties.numberingId));
    while (index < paragraphs.size()) {
        const auto paragraphLevel = std::clamp(
            paragraphs[index]->properties.numberingLevel, 0, 8);
        if (paragraphLevel < level) {
            break;
        }
        xml.writeStartElement(textNamespace(), QStringLiteral("list-item"));
        if (paragraphLevel > level) {
            writeListLevel(xml, paragraphs, index, level + 1, plan);
        } else {
            writeParagraph(xml, *paragraphs[index], plan);
            ++index;
            while (index < paragraphs.size()) {
                const auto nextLevel = std::clamp(
                    paragraphs[index]->properties.numberingLevel, 0, 8);
                if (nextLevel > level) {
                    writeListLevel(xml, paragraphs, index, level + 1, plan);
                } else if (nextLevel == level
                           && paragraphs[index]
                                  ->properties.numberingContinuation) {
                    writeParagraph(xml, *paragraphs[index], plan);
                    ++index;
                } else {
                    break;
                }
            }
        }
        xml.writeEndElement();
    }
    xml.writeEndElement();
}

void writeNumberedParagraphs(
    QXmlStreamWriter& xml,
    const std::vector<const WordParagraph*>& paragraphs,
    const WritePlan& plan)
{
    std::size_t index = 0;
    writeListLevel(xml, paragraphs, index, 0, plan);
}

void writeParagraphSequence(
    QXmlStreamWriter& xml,
    const std::vector<WordParagraph>& paragraphs,
    const WritePlan& plan)
{
    std::size_t index = 0;
    while (index < paragraphs.size()) {
        if (!paragraphs[index].properties.numberingId) {
            writeParagraph(xml, paragraphs[index], plan);
            ++index;
            continue;
        }
        const auto identifier = paragraphs[index].properties.numberingId;
        std::vector<const WordParagraph*> listParagraphs;
        while (index < paragraphs.size()
               && paragraphs[index].properties.numberingId == identifier) {
            listParagraphs.push_back(&paragraphs[index]);
            ++index;
        }
        writeNumberedParagraphs(xml, listParagraphs, plan);
    }
}

void writeTable(
    QXmlStreamWriter& xml,
    const WordTable& table,
    const WritePlan& plan,
    int tableIndex)
{
    xml.writeStartElement(tableNamespace(), QStringLiteral("table"));
    xml.writeAttribute(tableNamespace(), QStringLiteral("name"),
                       QStringLiteral("IGDTable") + QString::number(tableIndex));
    std::size_t columnCount = 1;
    for (const auto& row : table.rows) {
        columnCount = std::max(columnCount, row.cells.size());
    }
    xml.writeStartElement(tableNamespace(), QStringLiteral("table-column"));
    if (columnCount > 1) {
        xml.writeAttribute(
            tableNamespace(), QStringLiteral("number-columns-repeated"),
            QString::number(static_cast<qulonglong>(columnCount)));
    }
    xml.writeEndElement();
    for (const auto& row : table.rows) {
        xml.writeStartElement(tableNamespace(), QStringLiteral("table-row"));
        for (const auto& cell : row.cells) {
            xml.writeStartElement(tableNamespace(), QStringLiteral("table-cell"));
            xml.writeAttribute(officeNamespace(), QStringLiteral("value-type"), QStringLiteral("string"));
            if (cell.paragraphs.empty()) {
                xml.writeEmptyElement(textNamespace(), QStringLiteral("p"));
            } else {
                writeParagraphSequence(xml, cell.paragraphs, plan);
            }
            xml.writeEndElement();
        }
        xml.writeEndElement();
    }
    xml.writeEndElement();
}

void writeBody(QXmlStreamWriter& xml, const WordDocument& document, const WritePlan& plan)
{
    xml.writeStartElement(officeNamespace(), QStringLiteral("body"));
    xml.writeStartElement(officeNamespace(), QStringLiteral("text"));
    int tableIndex = 1;
    std::size_t blockIndex = 0;
    while (blockIndex < document.blocks().size()) {
        const auto* paragraph = std::get_if<WordParagraph>(
            &document.blocks()[blockIndex]);
        if (!paragraph) {
            writeTable(
                xml, std::get<WordTable>(document.blocks()[blockIndex]),
                plan, tableIndex++);
            ++blockIndex;
            continue;
        }
        if (!paragraph->properties.numberingId) {
            writeParagraph(xml, *paragraph, plan);
            ++blockIndex;
            continue;
        }

        const auto identifier = paragraph->properties.numberingId;
        std::vector<const WordParagraph*> listParagraphs;
        while (blockIndex < document.blocks().size()) {
            const auto* candidate = std::get_if<WordParagraph>(
                &document.blocks()[blockIndex]);
            if (!candidate || candidate->properties.numberingId != identifier) {
                break;
            }
            listParagraphs.push_back(candidate);
            ++blockIndex;
        }
        writeNumberedParagraphs(xml, listParagraphs, plan);
    }
    xml.writeEndElement();
    xml.writeEndElement();
}

std::optional<std::string> metadataValue(
    const WordDocument& document,
    const std::string& key)
{
    const auto found = document.metadata().find(key);
    if (found == document.metadata().end() || found->second.empty()) {
        return std::nullopt;
    }
    return found->second;
}

void writeMetadataSection(QXmlStreamWriter& xml, const WordDocument& document)
{
    xml.writeStartElement(officeNamespace(), QStringLiteral("meta"));
    xml.writeTextElement(
        metaNamespace(), QStringLiteral("generator"), QStringLiteral("iiGeneralDocument/0.1"));
    const auto write = [&](const QString& namespaceUri, const QString& name,
                           const std::string& key) {
        if (const auto value = metadataValue(document, key)) {
            xml.writeTextElement(namespaceUri, name, fromUtf8(*value));
        }
    };
    write(dcNamespace(), QStringLiteral("title"), "Title");
    write(dcNamespace(), QStringLiteral("subject"), "Subject");
    write(dcNamespace(), QStringLiteral("description"), "Description");
    write(metaNamespace(), QStringLiteral("initial-creator"), "Author");
    if (metadataValue(document, "LastModifiedBy")) {
        write(dcNamespace(), QStringLiteral("creator"), "LastModifiedBy");
    } else {
        write(dcNamespace(), QStringLiteral("creator"), "Author");
    }
    write(metaNamespace(), QStringLiteral("keyword"), "Keywords");
    write(metaNamespace(), QStringLiteral("creation-date"), "Created");
    write(dcNamespace(), QStringLiteral("date"), "Modified");
    xml.writeEndElement();
}

QByteArray contentXml(const WordDocument& document, const WritePlan& plan)
{
    return xmlDocument([&](QXmlStreamWriter& xml) {
        xml.writeStartElement(QStringLiteral("office:document-content"));
        writeCommonNamespaces(xml);
        xml.writeAttribute(officeNamespace(), QStringLiteral("version"), QStringLiteral("1.3"));
        writeFontFaces(xml, plan);
        writeAutomaticStyles(xml, plan, document.section(), true, false);
        writeBody(xml, document, plan);
        xml.writeEndElement();
    });
}

QByteArray stylesXml(const WordDocument& document, const WritePlan& plan)
{
    return xmlDocument([&](QXmlStreamWriter& xml) {
        xml.writeStartElement(QStringLiteral("office:document-styles"));
        writeCommonNamespaces(xml);
        xml.writeAttribute(officeNamespace(), QStringLiteral("version"), QStringLiteral("1.3"));
        writeFontFaces(xml, plan);
        writeNamedStylesAndLists(xml, plan);
        writeAutomaticStyles(xml, plan, document.section(), false, true);
        writeMasterStyles(xml);
        xml.writeEndElement();
    });
}

QByteArray metadataXml(const WordDocument& document)
{
    return xmlDocument([&](QXmlStreamWriter& xml) {
        xml.writeStartElement(QStringLiteral("office:document-meta"));
        writeCommonNamespaces(xml);
        xml.writeAttribute(officeNamespace(), QStringLiteral("version"), QStringLiteral("1.3"));
        writeMetadataSection(xml, document);
        xml.writeEndElement();
    });
}

QByteArray settingsXml()
{
    return xmlDocument([](QXmlStreamWriter& xml) {
        xml.writeStartElement(QStringLiteral("office:document-settings"));
        writeCommonNamespaces(xml);
        xml.writeAttribute(officeNamespace(), QStringLiteral("version"), QStringLiteral("1.3"));
        xml.writeEmptyElement(officeNamespace(), QStringLiteral("settings"));
        xml.writeEndElement();
    });
}

QByteArray flatXml(const WordDocument& document, const WritePlan& plan)
{
    return xmlDocument([&](QXmlStreamWriter& xml) {
        xml.writeStartElement(QStringLiteral("office:document"));
        writeCommonNamespaces(xml);
        xml.writeAttribute(officeNamespace(), QStringLiteral("version"), QStringLiteral("1.3"));
        xml.writeAttribute(officeNamespace(), QStringLiteral("mimetype"),
                           QString::fromLatin1(odtMimeType.data(),
                                               static_cast<qsizetype>(odtMimeType.size())));
        writeMetadataSection(xml, document);
        writeFontFaces(xml, plan);
        writeNamedStylesAndLists(xml, plan);
        writeAutomaticStyles(xml, plan, document.section(), true, true);
        writeMasterStyles(xml);
        writeBody(xml, document, plan);
        xml.writeEndElement();
    });
}

QByteArray manifestXml()
{
    return xmlDocument([](QXmlStreamWriter& xml) {
        xml.writeStartElement(QStringLiteral("manifest:manifest"));
        xml.writeNamespace(manifestNamespace(), QStringLiteral("manifest"));
        xml.writeAttribute(manifestNamespace(), QStringLiteral("version"), QStringLiteral("1.3"));
        const auto entry = [&](const QString& path, const QString& mediaType) {
            xml.writeStartElement(manifestNamespace(), QStringLiteral("file-entry"));
            xml.writeAttribute(manifestNamespace(), QStringLiteral("full-path"), path);
            xml.writeAttribute(manifestNamespace(), QStringLiteral("media-type"), mediaType);
            xml.writeEndElement();
        };
        entry(QStringLiteral("/"),
              QString::fromLatin1(odtMimeType.data(), static_cast<qsizetype>(odtMimeType.size())));
        entry(QStringLiteral("content.xml"), QStringLiteral("text/xml"));
        entry(QStringLiteral("styles.xml"), QStringLiteral("text/xml"));
        entry(QStringLiteral("meta.xml"), QStringLiteral("text/xml"));
        entry(QStringLiteral("settings.xml"), QStringLiteral("text/xml"));
        xml.writeEndElement();
    });
}

bool safePackagePath(std::string_view name)
{
    if (name.empty() || name.front() == '/' || name.find('\\') != std::string_view::npos) {
        return false;
    }
    std::size_t start = 0;
    while (start <= name.size()) {
        const auto separator = name.find('/', start);
        const auto component = name.substr(
            start, separator == std::string_view::npos ? name.size() - start
                                                       : separator - start);
        if (component == "..") {
            return false;
        }
        if (separator == std::string_view::npos) {
            break;
        }
        start = separator + 1;
    }
    return true;
}

std::uint16_t littleEndian16(const QByteArray& bytes, qsizetype offset)
{
    const auto* data = reinterpret_cast<const unsigned char*>(bytes.constData());
    return static_cast<std::uint16_t>(data[offset])
        | static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(data[offset + 1]) << 8U);
}

bool validateLocalMimetypeHeader(
    const std::filesystem::path& source,
    std::vector<Diagnostic>& diagnostics)
{
    QFile file(QString::fromStdString(source.string()));
    if (!file.open(QIODevice::ReadOnly)) {
        diagnostics.push_back(diagnostic(
            DiagnosticSeverity::error,
            "odf.local_header_open_failed",
            "The ODT local ZIP header could not be opened.",
            source));
        return false;
    }
    const auto header = file.read(30);
    const bool signatureValid = header.size() == 30
        && static_cast<unsigned char>(header[0]) == 0x50U
        && static_cast<unsigned char>(header[1]) == 0x4bU
        && static_cast<unsigned char>(header[2]) == 0x03U
        && static_cast<unsigned char>(header[3]) == 0x04U;
    if (!signatureValid) {
        diagnostics.push_back(diagnostic(
            DiagnosticSeverity::error,
            "odf.invalid_local_header",
            "The ODT package does not begin with a complete ZIP local-file header.",
            source));
        return false;
    }

    const auto flags = littleEndian16(header, 6);
    const auto method = littleEndian16(header, 8);
    const auto nameLength = littleEndian16(header, 26);
    const auto extraLength = littleEndian16(header, 28);
    const auto name = file.read(nameLength);
    const auto extra = file.read(extraLength);
    const auto mime = file.read(static_cast<qint64>(odtMimeType.size()));
    const auto expectedMime = QByteArray(
        odtMimeType.data(), static_cast<qsizetype>(odtMimeType.size()));
    if ((flags & 0x0001U) != 0U || method != ZIP_CM_STORE
        || name != QByteArrayLiteral("mimetype") || !extra.isEmpty()
        || mime != expectedMime) {
        diagnostics.push_back(diagnostic(
            DiagnosticSeverity::error,
            "odf.invalid_mimetype_local_header",
            "The first physical ODT ZIP entry must be an unencrypted, uncompressed "
            "mimetype entry with no local extra field.",
            source));
        return false;
    }
    return true;
}

bool validatePackageDirectory(
    zip_t* archive,
    const std::filesystem::path& source,
    std::vector<Diagnostic>& diagnostics,
    std::set<std::string>& entryNames)
{
    const auto count = zip_get_num_entries(archive, 0);
    if (count <= 0) {
        diagnostics.push_back(diagnostic(
            DiagnosticSeverity::error,
            "odf.empty_package",
            "The ODT ZIP package contains no entries.",
            source));
        return false;
    }
    if (count > maximumPackageEntries) {
        diagnostics.push_back(diagnostic(
            DiagnosticSeverity::error,
            "odf.too_many_package_entries",
            "The ODT ZIP package contains too many entries.",
            source));
        return false;
    }
    for (zip_int64_t index = 0; index < count; ++index) {
        const auto* rawName = zip_get_name(
            archive, static_cast<zip_uint64_t>(index), ZIP_FL_ENC_UTF_8);
        if (!rawName) {
            diagnostics.push_back(diagnostic(
                DiagnosticSeverity::error,
                "odf.invalid_entry_name",
                "The ODT package contains an unreadable entry name.",
                source));
            continue;
        }
        const std::string name{rawName};
        bool nameValid = true;
        if (name.size() > maximumPackageEntryNameBytes) {
            diagnostics.push_back(diagnostic(
                DiagnosticSeverity::error,
                "odf.entry_name_too_long",
                "The ODT package contains an entry name longer than the safety limit.",
                source));
            nameValid = false;
        } else if (!safePackagePath(name)) {
            diagnostics.push_back(diagnostic(
                DiagnosticSeverity::error,
                "odf.unsafe_entry_path",
                "The ODT package contains an unsafe entry path: " + name + ".",
                source));
            nameValid = false;
        }
        if (nameValid && !entryNames.insert(name).second) {
            diagnostics.push_back(diagnostic(
                DiagnosticSeverity::error,
                "odf.duplicate_package_entry",
                "The ODT package contains a duplicate entry: " + name + ".",
                source));
        }

        zip_stat_t status;
        zip_stat_init(&status);
        if (zip_stat_index(archive, static_cast<zip_uint64_t>(index), 0, &status) != 0
            || (status.valid & ZIP_STAT_COMP_METHOD) == 0U
            || (status.comp_method != ZIP_CM_STORE
                && status.comp_method != ZIP_CM_DEFLATE)) {
            diagnostics.push_back(diagnostic(
                DiagnosticSeverity::error,
                "odf.unsupported_zip_compression",
                "The ODT package uses a compression method outside the ODF contract.",
                source));
        }
        if ((status.valid & ZIP_STAT_ENCRYPTION_METHOD) == 0U
            || status.encryption_method != ZIP_EM_NONE) {
            diagnostics.push_back(diagnostic(
                DiagnosticSeverity::error,
                "odf.encryption_unsupported",
                "Encrypted ODT ZIP entries are not supported.",
                source));
        }
    }

    return !ii::document::hasErrors(diagnostics);
}

bool ensureDestinationDirectory(
    const std::filesystem::path& destination,
    std::vector<Diagnostic>& diagnostics,
    std::string_view codePrefix)
{
    if (destination.empty()) {
        diagnostics.push_back(diagnostic(
            DiagnosticSeverity::error,
            std::string(codePrefix) + ".destination_missing",
            "An OpenDocument destination path is required.",
            destination));
        return false;
    }
    std::error_code error;
    if (!destination.parent_path().empty()) {
        std::filesystem::create_directories(destination.parent_path(), error);
    }
    if (error) {
        diagnostics.push_back(diagnostic(
            DiagnosticSeverity::error,
            std::string(codePrefix) + ".destination_directory_failed",
            "The OpenDocument destination directory could not be created: "
                + error.message(),
            destination.parent_path()));
        return false;
    }
    return true;
}

struct PackagePart {
    std::string name;
    QByteArray bytes;
    zip_int32_t compressionMethod{ZIP_CM_DEFLATE};
};

} // namespace

WordReadResult readOdtPackage(
    const std::filesystem::path& source,
    const WordReadOptions& options)
{
    WordReadResult result;
    if (!std::filesystem::is_regular_file(source)) {
        result.diagnostics.push_back(diagnostic(
            DiagnosticSeverity::error,
            "odf.source_missing",
            "The ODT source does not exist.",
            source));
        return result;
    }
    if (options.maximumXmlPartBytes == 0) {
        result.diagnostics.push_back(diagnostic(
            DiagnosticSeverity::error,
            "odf.invalid_part_limit",
            "The OpenDocument XML-part size limit must be positive.",
            source));
        return result;
    }
    if (!validateLocalMimetypeHeader(source, result.diagnostics)) {
        return result;
    }

    int openError = 0;
    ZipArchive archive(zip_open(source.string().c_str(), ZIP_RDONLY, &openError));
    if (!archive.get()) {
        result.diagnostics.push_back(diagnostic(
            DiagnosticSeverity::error,
            "odf.open_failed",
            "The ODT ZIP package could not be opened: " + zipOpenError(openError),
            source));
        return result;
    }

    std::set<std::string> entryNames;
    if (!validatePackageDirectory(
            archive.get(), source, result.diagnostics, entryNames)) {
        return result;
    }
    const auto mime = readZipPart(
        archive.get(), "mimetype", 1024, source, result.diagnostics);
    if (!mime || std::string_view(mime->constData(), static_cast<std::size_t>(mime->size()))
            != odtMimeType) {
        result.diagnostics.push_back(diagnostic(
            DiagnosticSeverity::error,
            "odf.invalid_mimetype",
            "The ODT mimetype entry does not declare an OpenDocument text document.",
            source));
        return result;
    }

    const auto manifest = readZipPart(
        archive.get(), "META-INF/manifest.xml", options.maximumXmlPartBytes,
        source, result.diagnostics);
    if (!manifest) {
        return result;
    }
    std::set<std::string> declaredFiles;
    if (!validateManifest(*manifest, source, result.diagnostics, declaredFiles)) {
        return result;
    }
    if (!declaredFiles.contains("content.xml")) {
        result.diagnostics.push_back(diagnostic(
            DiagnosticSeverity::error,
            "odf.content_not_declared",
            "The ODT manifest does not declare content.xml.",
            source));
        return result;
    }
    for (const auto& name : entryNames) {
        if (name == "mimetype" || name.starts_with("META-INF/")
            || name.ends_with('/')) {
            continue;
        }
        if (!declaredFiles.contains(name)) {
            result.diagnostics.push_back(diagnostic(
                DiagnosticSeverity::error,
                "odf.package_entry_not_declared",
                "The ODT manifest does not declare package entry " + name + ".",
                source));
        }
    }
    for (const auto& name : declaredFiles) {
        if (!name.ends_with('/') && !entryNames.contains(name)) {
            result.diagnostics.push_back(diagnostic(
                DiagnosticSeverity::error,
                "odf.manifest_entry_missing",
                "The ODT manifest declares a package entry that is missing: " + name + ".",
                source));
        }
    }
    if (result.hasErrors()) {
        return result;
    }
    static const std::set<std::string> modeledEntries{
        "mimetype", "content.xml", "styles.xml", "meta.xml", "settings.xml",
        "META-INF/manifest.xml"};
    for (const auto& name : entryNames) {
        if (name.starts_with("META-INF/") && name.find("signatures") != std::string::npos) {
            warningOnce(
                result.diagnostics,
                "odf.digital_signature_not_preserved",
                "OpenDocument digital signatures can be read past but are not preserved by rewrite.",
                source);
        } else if (!modeledEntries.contains(name) && !name.ends_with('/')) {
            warningOnce(
                result.diagnostics,
                "odf.unmodeled_package_entries_not_preserved",
                "OpenDocument package entries outside the editable flow model are not preserved by rewrite.",
                source);
        }
    }

    const auto content = readZipPart(
        archive.get(), "content.xml", options.maximumXmlPartBytes,
        source, result.diagnostics);
    if (!content || !validateXmlEnvelope(
            *content, QStringLiteral("document-content"), false,
            "content.xml", source, result.diagnostics)) {
        return result;
    }

    StyleMap styles;
    FontFaceMap stylesFontFaces;
    PageStyleCatalog pageStyles;
    const auto defaultSection = result.document.section();
    if (const auto stylesPart = readZipPart(
            archive.get(), "styles.xml", options.maximumXmlPartBytes,
            source, result.diagnostics, false)) {
        if (!validateXmlEnvelope(
                *stylesPart, QStringLiteral("document-styles"), false,
                "styles.xml", source, result.diagnostics)) {
            return result;
        }
        stylesFontFaces = parseStyles(
            *stylesPart, styles, pageStyles, defaultSection,
            {});
    }
    parseStyles(
        *content, styles, pageStyles, defaultSection,
        stylesFontFaces);
    selectPageLayout(
        *content, styles, pageStyles, result.document.section(),
        source, result.diagnostics);

    if (const auto metadataPart = readZipPart(
            archive.get(), "meta.xml", options.maximumXmlPartBytes,
            source, result.diagnostics, false)) {
        if (!validateXmlEnvelope(
                *metadataPart, QStringLiteral("document-meta"), false,
                "meta.xml", source, result.diagnostics)) {
            return result;
        }
        parseMetadata(*metadataPart, result.document, source, result.diagnostics);
    }
    if (const auto settingsPart = readZipPart(
            archive.get(), "settings.xml", options.maximumXmlPartBytes,
            source, result.diagnostics, false)) {
        if (!validateXmlEnvelope(
                *settingsPart, QStringLiteral("document-settings"), false,
                "settings.xml", source, result.diagnostics)) {
            return result;
        }
    }
    parseBody(
        *content, styles, result.document, source, result.diagnostics,
        options.maximumXmlPartBytes);
    return result;
}

WordReadResult readFodtDocument(
    const std::filesystem::path& source,
    const WordReadOptions& options)
{
    WordReadResult result;
    const auto bytes = readFlatXml(source, options, result.diagnostics);
    if (!bytes || !validateXmlEnvelope(
            *bytes, QStringLiteral("document"), true,
            "flat document", source, result.diagnostics)) {
        return result;
    }
    StyleMap styles;
    PageStyleCatalog pageStyles;
    const auto defaultSection = result.document.section();
    parseStyles(
        *bytes, styles, pageStyles, defaultSection,
        {});
    selectPageLayout(
        *bytes, styles, pageStyles, result.document.section(),
        source, result.diagnostics);
    parseMetadata(*bytes, result.document, source, result.diagnostics);
    parseBody(
        *bytes, styles, result.document, source, result.diagnostics,
        options.maximumXmlPartBytes);
    return result;
}

WordWriteResult writeOdtPackage(
    const WordDocument& document,
    const std::filesystem::path& destination,
    const WordWriteOptions& options)
{
    WordWriteResult result;
    if (!ensureDestinationDirectory(destination, result.diagnostics, "odf")) {
        return result;
    }
    if (options.maximumXmlPartBytes == 0) {
        result.diagnostics.push_back(diagnostic(
            DiagnosticSeverity::error,
            "odf.invalid_part_limit",
            "The OpenDocument XML-part validation limit must be positive.",
            destination));
        return result;
    }
    validateDocument(document, destination, result.diagnostics);
    if (result.hasErrors()) {
        return result;
    }

    const auto plan = makeWritePlan(document);
    std::vector<PackagePart> parts;
    parts.push_back({
        "mimetype",
        QByteArray(odtMimeType.data(), static_cast<qsizetype>(odtMimeType.size())),
        ZIP_CM_STORE});
    parts.push_back({"content.xml", contentXml(document, plan), ZIP_CM_DEFLATE});
    parts.push_back({"styles.xml", stylesXml(document, plan), ZIP_CM_DEFLATE});
    parts.push_back({"meta.xml", metadataXml(document), ZIP_CM_DEFLATE});
    parts.push_back({"settings.xml", settingsXml(), ZIP_CM_DEFLATE});
    parts.push_back({"META-INF/manifest.xml", manifestXml(), ZIP_CM_DEFLATE});

    const auto parent = destination.parent_path().empty()
        ? std::filesystem::current_path() : destination.parent_path();
    const auto temporaryTemplate = parent
        / ("." + destination.filename().string() + ".XXXXXX");
    QTemporaryFile temporary(QString::fromStdString(temporaryTemplate.string()));
    temporary.setAutoRemove(true);
    if (!temporary.open()) {
        result.diagnostics.push_back(diagnostic(
            DiagnosticSeverity::error,
            "odf.temporary_file_failed",
            "Unable to create a same-directory temporary ODT package.",
            destination));
        return result;
    }
    const auto temporaryPath = std::filesystem::path(temporary.fileName().toStdString());
    temporary.close();

    int openError = 0;
    ZipArchive archive(zip_open(
        temporaryPath.string().c_str(), ZIP_CREATE | ZIP_TRUNCATE, &openError));
    if (!archive.get()) {
        result.diagnostics.push_back(diagnostic(
            DiagnosticSeverity::error,
            "odf.create_failed",
            "The temporary ODT package could not be created: " + zipOpenError(openError),
            destination));
        return result;
    }
    for (const auto& part : parts) {
        zip_source_t* source = zip_source_buffer(
            archive.get(), part.bytes.constData(),
            static_cast<zip_uint64_t>(part.bytes.size()), 0);
        if (!source) {
            result.diagnostics.push_back(diagnostic(
                DiagnosticSeverity::error,
                "odf.part_source_failed",
                "Unable to prepare ODT part " + part.name + ": "
                    + std::string(zip_strerror(archive.get())),
                destination));
            return result;
        }
        const auto index = zip_file_add(
            archive.get(), part.name.c_str(), source,
            ZIP_FL_ENC_UTF_8 | ZIP_FL_OVERWRITE);
        if (index < 0) {
            zip_source_free(source);
            result.diagnostics.push_back(diagnostic(
                DiagnosticSeverity::error,
                "odf.part_add_failed",
                "Unable to add ODT part " + part.name + ": "
                    + std::string(zip_strerror(archive.get())),
                destination));
            return result;
        }
        if (zip_set_file_compression(
                archive.get(), static_cast<zip_uint64_t>(index),
                part.compressionMethod, part.compressionMethod == ZIP_CM_DEFLATE ? 6 : 0) != 0) {
            result.diagnostics.push_back(diagnostic(
                DiagnosticSeverity::error,
                "odf.part_compression_failed",
                "Unable to set ODT part compression for " + part.name + ": "
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
            "odf.commit_failed",
            "The temporary ODT package could not be committed: " + message,
            destination));
        return result;
    }

    WordReadOptions validationOptions;
    validationOptions.maximumXmlPartBytes = options.maximumXmlPartBytes;
    const auto validation = readOdtPackage(temporaryPath, validationOptions);
    if (validation.hasErrors()) {
        result.diagnostics.push_back(diagnostic(
            DiagnosticSeverity::error,
            "odf.post_write_validation_failed",
            "The generated ODT package could not be reopened before commit.",
            destination));
        result.diagnostics.insert(
            result.diagnostics.end(), validation.diagnostics.begin(), validation.diagnostics.end());
        return result;
    }

    const auto commit = atomicReplacePreservingPermissions(
        temporaryPath, destination);
    if (!commit.succeeded) {
        result.diagnostics.push_back(diagnostic(
            DiagnosticSeverity::error,
            "odf." + commit.diagnosticSuffix,
            commit.message,
            destination));
        return result;
    }
    temporary.setAutoRemove(false);
    return result;
}

WordWriteResult writeFodtDocument(
    const WordDocument& document,
    const std::filesystem::path& destination,
    const WordWriteOptions& options)
{
    WordWriteResult result;
    if (!ensureDestinationDirectory(destination, result.diagnostics, "fodt")) {
        return result;
    }
    if (options.maximumXmlPartBytes == 0) {
        result.diagnostics.push_back(diagnostic(
            DiagnosticSeverity::error,
            "fodt.invalid_part_limit",
            "The FODT XML validation limit must be positive.",
            destination));
        return result;
    }
    validateDocument(document, destination, result.diagnostics);
    if (result.hasErrors()) {
        return result;
    }
    const auto bytes = flatXml(document, makeWritePlan(document));
    if (!validateXmlEnvelope(
            bytes, QStringLiteral("document"), true,
            "generated flat document", destination, result.diagnostics)) {
        return result;
    }

    const auto parent = destination.parent_path().empty()
        ? std::filesystem::current_path() : destination.parent_path();
    const auto temporaryTemplate = parent
        / ("." + destination.filename().string() + ".XXXXXX");
    QTemporaryFile temporary(QString::fromStdString(temporaryTemplate.string()));
    temporary.setAutoRemove(true);
    if (!temporary.open()) {
        result.diagnostics.push_back(diagnostic(
            DiagnosticSeverity::error,
            "fodt.temporary_file_failed",
            "Unable to create a same-directory temporary FODT document: "
                + toUtf8(temporary.errorString()),
            destination));
        return result;
    }
    if (temporary.write(bytes) != bytes.size() || !temporary.flush()) {
        result.diagnostics.push_back(diagnostic(
            DiagnosticSeverity::error,
            "fodt.temporary_write_failed",
            "The temporary FODT document could not be written: "
                + toUtf8(temporary.errorString()),
            destination));
        return result;
    }
    const auto temporaryPath = std::filesystem::path(
        temporary.fileName().toStdString());
    temporary.close();

    WordReadOptions validationOptions;
    validationOptions.maximumXmlPartBytes = options.maximumXmlPartBytes;
    const auto validation = readFodtDocument(temporaryPath, validationOptions);
    if (validation.hasErrors()) {
        result.diagnostics.push_back(diagnostic(
            DiagnosticSeverity::error,
            "fodt.post_write_validation_failed",
            "The generated FODT document could not be reopened before commit.",
            destination));
        result.diagnostics.insert(
            result.diagnostics.end(), validation.diagnostics.begin(), validation.diagnostics.end());
        return result;
    }
    const auto commit = atomicReplacePreservingPermissions(
        temporaryPath, destination);
    if (!commit.succeeded) {
        result.diagnostics.push_back(diagnostic(
            DiagnosticSeverity::error,
            "fodt." + commit.diagnosticSuffix,
            commit.message,
            destination));
        return result;
    }
    temporary.setAutoRemove(false);
    return result;
}

} // namespace ii::document::detail
