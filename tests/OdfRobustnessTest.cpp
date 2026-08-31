#include "TestSupport.h"
#include "Word/WordDocumentReader.h"
#include "Word/WordDocumentWriter.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <numeric>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

using namespace ii::document;

namespace {

constexpr std::string_view odtMimeType =
    "application/vnd.oasis.opendocument.text";

constexpr std::string_view officeNamespace =
    "urn:oasis:names:tc:opendocument:xmlns:office:1.0";
constexpr std::string_view textNamespace =
    "urn:oasis:names:tc:opendocument:xmlns:text:1.0";
constexpr std::string_view tableNamespace =
    "urn:oasis:names:tc:opendocument:xmlns:table:1.0";
constexpr std::string_view manifestNamespace =
    "urn:oasis:names:tc:opendocument:xmlns:manifest:1.0";

struct ZipEntry {
    std::string name;
    std::string data;
    std::optional<std::uint32_t> crcOverride;
    bool encrypted{false};
};

void printDiagnostics(const std::vector<Diagnostic>& diagnostics)
{
    for (const auto& item : diagnostics) {
        std::cerr << item.code << ": " << item.message << " [" << item.context << "]\n";
    }
}

bool hasDiagnostic(
    const std::vector<Diagnostic>& diagnostics,
    std::string_view code)
{
    return std::ranges::any_of(diagnostics, [&](const Diagnostic& item) {
        return item.code == code;
    });
}

std::uint32_t crc32(std::string_view value)
{
    std::uint32_t crc = 0xffffffffU;
    for (const auto character : value) {
        crc ^= static_cast<unsigned char>(character);
        for (int bit = 0; bit < 8; ++bit) {
            const auto lowBitMask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (0xedb88320U & lowBitMask);
        }
    }
    return ~crc;
}

void appendLittleEndian16(std::vector<std::uint8_t>& output, std::uint16_t value)
{
    output.push_back(static_cast<std::uint8_t>(value & 0xffU));
    output.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
}

void appendLittleEndian32(std::vector<std::uint8_t>& output, std::uint32_t value)
{
    output.push_back(static_cast<std::uint8_t>(value & 0xffU));
    output.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
    output.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xffU));
    output.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xffU));
}

void appendString(std::vector<std::uint8_t>& output, std::string_view value)
{
    output.insert(output.end(), value.begin(), value.end());
}

void writeBinaryFile(
    const std::filesystem::path& path,
    const std::vector<std::uint8_t>& bytes)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    expect(output.is_open(), "the robustness ZIP fixture can be opened");
    output.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    expect(static_cast<bool>(output), "the complete robustness ZIP fixture is written");
}

void writeTextFile(const std::filesystem::path& path, std::string_view text)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    expect(output.is_open(), "the robustness XML fixture can be opened");
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    expect(static_cast<bool>(output), "the complete robustness XML fixture is written");
}

std::string readFile(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    expect(input.is_open(), "the OpenDocument transaction fixture can be reopened");
    return {
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
}

void writeStoredZip(
    const std::filesystem::path& path,
    const std::vector<ZipEntry>& entries,
    std::vector<std::size_t> centralDirectoryOrder = {})
{
    expect(!entries.empty(), "a robustness ZIP fixture has entries");
    expect(entries.size() <= std::numeric_limits<std::uint16_t>::max(),
           "a robustness ZIP fixture fits the classic ZIP entry-count field");

    if (centralDirectoryOrder.empty()) {
        centralDirectoryOrder.resize(entries.size());
        std::iota(
            centralDirectoryOrder.begin(), centralDirectoryOrder.end(),
            std::size_t{0});
    }
    expect(centralDirectoryOrder.size() == entries.size(),
           "the central-directory order covers every ZIP entry");

    std::vector<std::uint8_t> bytes;
    std::vector<std::uint32_t> localOffsets(entries.size());
    std::vector<std::uint32_t> storedCrcs(entries.size());
    constexpr std::uint16_t utf8Flag = 0x0800U;

    for (std::size_t index = 0; index < entries.size(); ++index) {
        const auto& entry = entries[index];
        expect(entry.name.size() <= std::numeric_limits<std::uint16_t>::max(),
               "a ZIP fixture entry name fits its local-header field");
        expect(entry.data.size() <= std::numeric_limits<std::uint32_t>::max(),
               "a ZIP fixture entry fits its classic size field");
        expect(bytes.size() <= std::numeric_limits<std::uint32_t>::max(),
               "a ZIP fixture local offset fits its classic field");

        localOffsets[index] = static_cast<std::uint32_t>(bytes.size());
        storedCrcs[index] = entry.crcOverride.value_or(crc32(entry.data));
        const auto dataSize = static_cast<std::uint32_t>(entry.data.size());

        appendLittleEndian32(bytes, 0x04034b50U);
        appendLittleEndian16(bytes, 20U);
        appendLittleEndian16(
            bytes, static_cast<std::uint16_t>(
                       utf8Flag | (entry.encrypted ? 0x0001U : 0U)));
        appendLittleEndian16(bytes, 0U);
        appendLittleEndian16(bytes, 0U);
        appendLittleEndian16(bytes, 0U);
        appendLittleEndian32(bytes, storedCrcs[index]);
        appendLittleEndian32(bytes, dataSize);
        appendLittleEndian32(bytes, dataSize);
        appendLittleEndian16(bytes, static_cast<std::uint16_t>(entry.name.size()));
        appendLittleEndian16(bytes, 0U);
        appendString(bytes, entry.name);
        appendString(bytes, entry.data);
    }

    expect(bytes.size() <= std::numeric_limits<std::uint32_t>::max(),
           "the ZIP fixture central-directory offset fits its classic field");
    const auto centralDirectoryOffset = static_cast<std::uint32_t>(bytes.size());
    for (const auto index : centralDirectoryOrder) {
        expect(index < entries.size(), "a central-directory index is valid");
        const auto& entry = entries[index];
        const auto dataSize = static_cast<std::uint32_t>(entry.data.size());

        appendLittleEndian32(bytes, 0x02014b50U);
        appendLittleEndian16(bytes, 20U);
        appendLittleEndian16(bytes, 20U);
        appendLittleEndian16(
            bytes, static_cast<std::uint16_t>(
                       utf8Flag | (entry.encrypted ? 0x0001U : 0U)));
        appendLittleEndian16(bytes, 0U);
        appendLittleEndian16(bytes, 0U);
        appendLittleEndian16(bytes, 0U);
        appendLittleEndian32(bytes, storedCrcs[index]);
        appendLittleEndian32(bytes, dataSize);
        appendLittleEndian32(bytes, dataSize);
        appendLittleEndian16(bytes, static_cast<std::uint16_t>(entry.name.size()));
        appendLittleEndian16(bytes, 0U);
        appendLittleEndian16(bytes, 0U);
        appendLittleEndian16(bytes, 0U);
        appendLittleEndian16(bytes, 0U);
        appendLittleEndian32(bytes, 0U);
        appendLittleEndian32(bytes, localOffsets[index]);
        appendString(bytes, entry.name);
    }

    expect(bytes.size() <= std::numeric_limits<std::uint32_t>::max(),
           "the ZIP fixture central-directory end fits its classic field");
    const auto centralDirectorySize = static_cast<std::uint32_t>(bytes.size())
        - centralDirectoryOffset;
    const auto entryCount = static_cast<std::uint16_t>(entries.size());
    appendLittleEndian32(bytes, 0x06054b50U);
    appendLittleEndian16(bytes, 0U);
    appendLittleEndian16(bytes, 0U);
    appendLittleEndian16(bytes, entryCount);
    appendLittleEndian16(bytes, entryCount);
    appendLittleEndian32(bytes, centralDirectorySize);
    appendLittleEndian32(bytes, centralDirectoryOffset);
    appendLittleEndian16(bytes, 0U);

    writeBinaryFile(path, bytes);
}

std::string minimalContentXml()
{
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<office:document-content xmlns:office=\"" + std::string(officeNamespace)
        + "\" xmlns:text=\"" + std::string(textNamespace)
        + "\" office:version=\"1.3\">"
          "<office:body><office:text><text:p>safe</text:p></office:text></office:body>"
          "</office:document-content>";
}

std::string minimalManifestXml()
{
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<manifest:manifest xmlns:manifest=\"" + std::string(manifestNamespace)
        + "\" manifest:version=\"1.3\">"
          "<manifest:file-entry manifest:full-path=\"/\" manifest:media-type=\""
        + std::string(odtMimeType)
        + "\"/>"
          "<manifest:file-entry manifest:full-path=\"content.xml\" "
          "manifest:media-type=\"text/xml\"/>"
          "</manifest:manifest>";
}

std::vector<ZipEntry> minimalOdtEntries()
{
    return {
        {"mimetype", std::string(odtMimeType), std::nullopt, false},
        {"content.xml", minimalContentXml(), std::nullopt, false},
        {"META-INF/manifest.xml", minimalManifestXml(), std::nullopt, false},
    };
}

std::string flatDocument(std::string_view body)
{
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<office:document xmlns:office=\"" + std::string(officeNamespace)
        + "\" xmlns:text=\"" + std::string(textNamespace)
        + "\" xmlns:table=\"" + std::string(tableNamespace)
        + "\" office:version=\"1.3\" office:mimetype=\""
        + std::string(odtMimeType)
        + "\"><office:body><office:text>" + std::string(body)
        + "</office:text></office:body></office:document>";
}

WordReadResult readExpectingError(
    const std::filesystem::path& path,
    std::string_view code,
    const WordReadOptions& options = {})
{
    auto result = WordDocumentReader{}.read(path, options);
    if (!result.hasErrors() || !hasDiagnostic(result.diagnostics, code)) {
        printDiagnostics(result.diagnostics);
    }
    expect(result.hasErrors(), "a hostile OpenDocument fixture fails closed");
    expect(hasDiagnostic(result.diagnostics, code),
           "a hostile OpenDocument fixture reports its specific diagnostic");
    return result;
}

void crcMismatchFailsClosed(const std::filesystem::path& outputDirectory)
{
    auto entries = minimalOdtEntries();
    entries[1].crcOverride = crc32(entries[1].data) ^ 0xffffffffU;
    const auto path = outputDirectory / "odf-robustness-bad-crc.odt";
    writeStoredZip(path, entries);
    (void)readExpectingError(path, "odf.part_integrity_failed");
}

void physicalMimetypeOrderIsAuthoritative(
    const std::filesystem::path& outputDirectory)
{
    auto physicalContentFirst = minimalOdtEntries();
    std::swap(physicalContentFirst[0], physicalContentFirst[1]);
    const auto invalidPath = outputDirectory / "odf-robustness-content-first.odt";
    writeStoredZip(invalidPath, physicalContentFirst, {1, 0, 2});
    (void)readExpectingError(invalidPath, "odf.invalid_mimetype_local_header");

    const auto validPath = outputDirectory / "odf-robustness-central-reordered.odt";
    writeStoredZip(validPath, minimalOdtEntries(), {2, 1, 0});
    const auto valid = WordDocumentReader{}.read(validPath);
    if (valid.hasErrors()) {
        printDiagnostics(valid.diagnostics);
    }
    expect(!valid.hasErrors(),
           "an ODT with physical mimetype first is accepted regardless of central order");
}

void encryptedZipEntryFailsClosed(const std::filesystem::path& outputDirectory)
{
    auto entries = minimalOdtEntries();
    entries[1].encrypted = true;
    const auto path = outputDirectory / "odf-robustness-encrypted-entry.odt";
    writeStoredZip(path, entries);
    (void)readExpectingError(path, "odf.encryption_unsupported");
}

void packageEntryLimitFailsClosed(const std::filesystem::path& outputDirectory)
{
    auto entries = minimalOdtEntries();
    entries.reserve(10'001);
    for (std::size_t index = 0; index < 9'998; ++index) {
        entries.push_back({
            "META-INF/padding-" + std::to_string(index), {}, std::nullopt, false});
    }
    expect(entries.size() == 10'001, "the package-limit fixture has 10,001 entries");

    const auto path = outputDirectory / "odf-robustness-package-count.odt";
    writeStoredZip(path, entries);
    (void)readExpectingError(path, "odf.too_many_package_entries");
}

void manifestEntryLimitFailsClosed(const std::filesystem::path& outputDirectory)
{
    std::string manifest =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<manifest:manifest xmlns:manifest=\"" + std::string(manifestNamespace)
        + "\" manifest:version=\"1.3\">"
          "<manifest:file-entry manifest:full-path=\"/\" manifest:media-type=\""
        + std::string(odtMimeType)
        + "\"/>"
          "<manifest:file-entry manifest:full-path=\"content.xml\" "
          "manifest:media-type=\"text/xml\"/>";
    for (std::size_t index = 0; index < 9'999; ++index) {
        manifest += "<manifest:file-entry manifest:full-path=\"ghost/"
            + std::to_string(index) + "\" manifest:media-type=\"\"/>";
    }
    manifest += "</manifest:manifest>";

    auto entries = minimalOdtEntries();
    entries[2].data = std::move(manifest);
    const auto path = outputDirectory / "odf-robustness-manifest-count.odt";
    writeStoredZip(path, entries);
    (void)readExpectingError(path, "odf.too_many_manifest_entries");
}

void repeatedTableMultiplicationFailsClosed(
    const std::filesystem::path& outputDirectory)
{
    const auto path = outputDirectory / "odf-robustness-repeat-product.fodt";
    const auto xml = flatDocument(
        "<table:table>"
        "<table:table-row table:number-rows-repeated=\"1000\">"
        "<table:table-cell table:number-columns-repeated=\"1001\">"
        "<text:p>x</text:p>"
        "</table:table-cell>"
        "</table:table-row>"
        "</table:table>");
    writeTextFile(path, xml);
    (void)readExpectingError(path, "odf.model_expansion_limit_exceeded");
}

void cumulativeSpacesFailClosed(const std::filesystem::path& outputDirectory)
{
    std::string body = "<text:p>";
    for (int index = 0; index < 6; ++index) {
        body += "<text:s text:c=\"1000\"/>";
    }
    body += "</text:p>";

    const auto path = outputDirectory / "odf-robustness-cumulative-space.fodt";
    const auto xml = flatDocument(body);
    expect(xml.size() < 4'096,
           "the cumulative-space XML is smaller than its configured input limit");
    writeTextFile(path, xml);

    WordReadOptions options;
    options.maximumXmlPartBytes = 4'096;
    (void)readExpectingError(path, "odf.text_expansion_limit_exceeded", options);
}

void semanticNestingFailsClosed(const std::filesystem::path& outputDirectory)
{
    std::string body = "<text:p>";
    for (int depth = 0; depth < 130; ++depth) {
        body += "<text:span>";
    }
    body += "nested";
    for (int depth = 0; depth < 130; ++depth) {
        body += "</text:span>";
    }
    body += "</text:p>";

    const auto path = outputDirectory / "odf-robustness-nesting.fodt";
    writeTextFile(path, flatDocument(body));
    (void)readExpectingError(path, "odf.nesting_limit_exceeded");
}

WordDocument overwriteDocument(std::string text)
{
    WordDocument document;
    WordParagraph paragraph;
    paragraph.runs.push_back({std::move(text), {}});
    document.appendParagraph(std::move(paragraph));
    return document;
}

#ifndef _WIN32
std::filesystem::perms ordinaryNewFilePermissions(
    const std::filesystem::path& outputDirectory)
{
    const auto referencePath =
        outputDirectory / "odf-robustness-ordinary-new-file-mode.reference";
    std::error_code removeError;
    std::filesystem::remove(referencePath, removeError);
    expect(!removeError, "the ordinary-mode reference can be reset");

    const int descriptor = ::open(
        referencePath.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0666);
    expect(descriptor >= 0, "an ordinary 0666-mode reference file can be created");

    struct stat status {};
    expect(::fstat(descriptor, &status) == 0,
           "the ordinary new-file mode can be inspected");
    expect(::close(descriptor) == 0,
           "the ordinary-mode reference descriptor can be closed");
    std::filesystem::remove(referencePath, removeError);
    expect(!removeError, "the ordinary-mode reference file can be removed");

    return static_cast<std::filesystem::perms>(status.st_mode)
        & std::filesystem::perms::all;
}

void newDestinationUsesOrdinaryPermissions(
    const std::filesystem::path& outputDirectory,
    std::string_view extension,
    std::filesystem::perms expectedPermissions)
{
    const auto path = outputDirectory
        / ("odf-robustness-new-permissions" + std::string(extension));
    std::error_code removeError;
    std::filesystem::remove(path, removeError);
    expect(!removeError, "the new-destination permission fixture can be reset");
    expect(!std::filesystem::exists(path),
           "the permission fixture destination starts absent");

    const auto written = WordDocumentWriter{}.write(
        overwriteDocument("new destination permissions"), path);
    if (written.hasErrors()) {
        printDiagnostics(written.diagnostics);
    }
    expect(!written.hasErrors(), "a new Word-format destination is written");

    std::error_code statusError;
    const auto actualPermissions =
        std::filesystem::status(path, statusError).permissions()
        & std::filesystem::perms::all;
    expect(!statusError, "the new Word-format destination mode is readable");
    expect(actualPermissions == expectedPermissions,
           "a new Word-format destination uses the ordinary 0666 creation mode");
}
#endif

void permissionsSurviveOverwrite(
    const std::filesystem::path& outputDirectory,
    std::string_view extension)
{
    const auto path = outputDirectory
        / ("odf-robustness-permissions" + std::string(extension));
    auto first = overwriteDocument("before overwrite");
    const auto initialWrite = WordDocumentWriter{}.write(first, path);
    if (initialWrite.hasErrors()) {
        printDiagnostics(initialWrite.diagnostics);
    }
    expect(!initialWrite.hasErrors(), "the permission fixture is initially written");

#ifndef _WIN32
    constexpr auto mode0644 = std::filesystem::perms::owner_read
        | std::filesystem::perms::owner_write
        | std::filesystem::perms::group_read
        | std::filesystem::perms::others_read;
    std::error_code permissionError;
    std::filesystem::permissions(
        path, mode0644, std::filesystem::perm_options::replace, permissionError);
    expect(!permissionError, "the permission fixture can be changed to mode 0644");
#endif

    auto replacement = overwriteDocument("after overwrite");
    const auto overwritten = WordDocumentWriter{}.write(replacement, path);
    if (overwritten.hasErrors()) {
        printDiagnostics(overwritten.diagnostics);
    }
    expect(!overwritten.hasErrors(), "an existing OpenDocument file is overwritten");

#ifndef _WIN32
    std::error_code statusError;
    const auto actual = std::filesystem::status(path, statusError).permissions()
        & std::filesystem::perms::all;
    expect(!statusError, "the overwritten OpenDocument permissions are readable");
    expect(actual == mode0644,
           "an OpenDocument overwrite preserves the existing 0644 mode");
#endif

    const auto reopened = WordDocumentReader{}.read(path);
    expect(!reopened.hasErrors(), "the permission-preserving replacement remains readable");
}

void validationFailurePreservesDestination(
    const std::filesystem::path& outputDirectory,
    std::string_view extension)
{
    const auto path = outputDirectory
        / ("odf-robustness-transaction" + std::string(extension));
    const auto initial = WordDocumentWriter{}.write(
        overwriteDocument("committed original"), path);
    expect(!initial.hasErrors(), "the transaction fixture is initially committed");
    const auto originalBytes = readFile(path);

    WordWriteOptions restrictive;
    restrictive.maximumXmlPartBytes = 1;
    const auto rejected = WordDocumentWriter{}.write(
        overwriteDocument("replacement must not commit"), path, restrictive);
    expect(rejected.hasErrors(),
           "post-generation validation can reject an OpenDocument replacement");
    expect(readFile(path) == originalBytes,
           "a rejected OpenDocument replacement leaves the existing bytes untouched");
}

} // namespace

int main()
{
    expect(crc32("123456789") == 0xcbf43926U,
           "the fixture ZIP CRC implementation matches the standard vector");

    const std::filesystem::path outputDirectory{IIGENERALDOCUMENT_TEST_OUTPUT_DIR};
    std::filesystem::create_directories(outputDirectory);

    crcMismatchFailsClosed(outputDirectory);
    physicalMimetypeOrderIsAuthoritative(outputDirectory);
    encryptedZipEntryFailsClosed(outputDirectory);
    packageEntryLimitFailsClosed(outputDirectory);
    manifestEntryLimitFailsClosed(outputDirectory);
    repeatedTableMultiplicationFailsClosed(outputDirectory);
    cumulativeSpacesFailClosed(outputDirectory);
    semanticNestingFailsClosed(outputDirectory);
#ifndef _WIN32
    const auto expectedNewFilePermissions =
        ordinaryNewFilePermissions(outputDirectory);
    newDestinationUsesOrdinaryPermissions(
        outputDirectory, ".docx", expectedNewFilePermissions);
    newDestinationUsesOrdinaryPermissions(
        outputDirectory, ".odt", expectedNewFilePermissions);
    newDestinationUsesOrdinaryPermissions(
        outputDirectory, ".fodt", expectedNewFilePermissions);
#endif
    permissionsSurviveOverwrite(outputDirectory, ".odt");
    permissionsSurviveOverwrite(outputDirectory, ".fodt");
    validationFailurePreservesDestination(outputDirectory, ".odt");
    validationFailurePreservesDestination(outputDirectory, ".fodt");
}
