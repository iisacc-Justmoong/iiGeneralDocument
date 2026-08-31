#include "TestSupport.h"
#include "Word/WordDocumentReader.h"

#include <zip.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
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

void writeTextFile(const std::filesystem::path& path, std::string_view contents)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    expect(output.is_open(), "the style compatibility fixture can be created");
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    expect(output.good(), "the complete style compatibility fixture is written");
}

void addPackagePart(
    zip_t* archive,
    std::string_view name,
    std::string_view contents,
    zip_int32_t compressionMethod)
{
    auto* source = zip_source_buffer(
        archive, contents.data(), static_cast<zip_uint64_t>(contents.size()), 0);
    expect(source != nullptr, "an ODT compatibility part source is created");
    const std::string partName{name};
    const auto index = zip_file_add(
        archive, partName.c_str(), source, ZIP_FL_ENC_UTF_8 | ZIP_FL_OVERWRITE);
    if (index < 0) {
        zip_source_free(source);
    }
    expect(index >= 0, "an ODT compatibility part is added");
    expect(
        zip_set_file_compression(
            archive,
            static_cast<zip_uint64_t>(index),
            compressionMethod,
            compressionMethod == ZIP_CM_DEFLATE ? 6 : 0) == 0,
        "the ODT compatibility part compression is configured");
}

void writeFontAliasOdt(const std::filesystem::path& path)
{
    constexpr std::string_view manifest = R"xml(<?xml version="1.0" encoding="UTF-8"?>
<manifest:manifest xmlns:manifest="urn:oasis:names:tc:opendocument:xmlns:manifest:1.0" manifest:version="1.3">
  <manifest:file-entry manifest:full-path="/" manifest:media-type="application/vnd.oasis.opendocument.text"/>
  <manifest:file-entry manifest:full-path="content.xml" manifest:media-type="text/xml"/>
  <manifest:file-entry manifest:full-path="styles.xml" manifest:media-type="text/xml"/>
</manifest:manifest>)xml";
    constexpr std::string_view styles = R"xml(<?xml version="1.0" encoding="UTF-8"?>
<office:document-styles xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0" xmlns:svg="urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0" office:version="1.3">
  <office:font-face-decls>
    <style:font-face style:name="FallbackAlias" svg:font-family="'Fallback Sans'"/>
    <style:font-face style:name="SharedAlias" svg:font-family="'Styles Serif'"/>
  </office:font-face-decls>
  <office:styles>
    <style:style style:name="NamedText" style:family="text">
      <style:text-properties style:font-name="SharedAlias"/>
    </style:style>
  </office:styles>
</office:document-styles>)xml";
    constexpr std::string_view content = R"xml(<?xml version="1.0" encoding="UTF-8"?>
<office:document-content xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0" xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" xmlns:svg="urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0" office:version="1.3">
  <office:font-face-decls>
    <style:font-face style:name="SharedAlias" svg:font-family="'Content Sans'"/>
  </office:font-face-decls>
  <office:automatic-styles>
    <style:style style:name="FallbackText" style:family="text">
      <style:text-properties style:font-name="FallbackAlias"/>
    </style:style>
    <style:style style:name="ContentText" style:family="text">
      <style:text-properties style:font-name="SharedAlias"/>
    </style:style>
  </office:automatic-styles>
  <office:body><office:text>
    <text:p><text:span text:style-name="FallbackText">fallback</text:span></text:p>
    <text:p><text:span text:style-name="ContentText">content</text:span></text:p>
    <text:p><text:span text:style-name="NamedText">named</text:span></text:p>
  </office:text></office:body>
</office:document-content>)xml";

    int openError = 0;
    auto* archive = zip_open(
        path.string().c_str(), ZIP_CREATE | ZIP_TRUNCATE, &openError);
    expect(archive != nullptr, "the ODT style compatibility package is opened");
    addPackagePart(archive, "mimetype", odtMimeType, ZIP_CM_STORE);
    addPackagePart(archive, "META-INF/manifest.xml", manifest, ZIP_CM_DEFLATE);
    addPackagePart(archive, "content.xml", content, ZIP_CM_DEFLATE);
    addPackagePart(archive, "styles.xml", styles, ZIP_CM_DEFLATE);
    if (zip_close(archive) != 0) {
        zip_discard(archive);
        expect(false, "the ODT style compatibility package is committed");
    }
}

void verifyStyleFamilies(const std::filesystem::path& outputDirectory)
{
    constexpr std::string_view fodt = R"xml(<?xml version="1.0" encoding="UTF-8"?>
<office:document xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0" xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0" office:version="1.3" office:mimetype="application/vnd.oasis.opendocument.text">
  <office:styles>
    <style:style style:name="SharedBase" style:family="paragraph">
      <style:paragraph-properties fo:text-align="center"/>
    </style:style>
    <style:style style:name="SharedBase" style:family="text">
      <style:text-properties fo:font-weight="bold"/>
    </style:style>
    <style:style style:name="Shared" style:family="paragraph" style:parent-style-name="SharedBase"/>
    <style:style style:name="Shared" style:family="text" style:parent-style-name="SharedBase"/>
  </office:styles>
  <office:body><office:text>
    <text:p text:style-name="Shared"><text:span text:style-name="Shared">family</text:span></text:p>
  </office:text></office:body>
</office:document>)xml";
    const auto path = outputDirectory / "odf-style-family.fodt";
    writeTextFile(path, fodt);

    const auto read = WordDocumentReader{}.read(path);
    printDiagnostics(read.diagnostics);
    expect(!read.hasErrors(), "same-name paragraph and text style families are readable");
    const auto* paragraph = findParagraph(read.document, "family");
    expect(paragraph != nullptr, "the family compatibility paragraph is present");
    expect(
        paragraph->properties.alignment == WordParagraphAlignment::center,
        "paragraph style inheritance resolves within the paragraph family");
    expect(
        paragraph->runs.size() == 1 && paragraph->runs.front().properties.bold,
        "text style inheritance resolves within the text family");
}

void verifyPartScopedFontAliases(const std::filesystem::path& outputDirectory)
{
    const auto path = outputDirectory / "odf-font-alias-scope.odt";
    writeFontAliasOdt(path);

    const auto read = WordDocumentReader{}.read(path);
    printDiagnostics(read.diagnostics);
    expect(!read.hasErrors(), "cross-part ODF font aliases are readable");

    const auto* fallback = findParagraph(read.document, "fallback");
    expect(
        fallback != nullptr && fallback->runs.size() == 1
            && fallback->runs.front().properties.fontFamily == "Fallback Sans",
        "content styles resolve font aliases declared only in styles.xml");

    const auto* content = findParagraph(read.document, "content");
    expect(
        content != nullptr && content->runs.size() == 1
            && content->runs.front().properties.fontFamily == "Content Sans",
        "content-local font aliases take precedence over styles.xml aliases");

    const auto* named = findParagraph(read.document, "named");
    expect(
        named != nullptr && named->runs.size() == 1
            && named->runs.front().properties.fontFamily == "Styles Serif",
        "styles.xml styles retain their part-local font alias meaning");
}

void verifyMasterPageLayoutSelection(const std::filesystem::path& outputDirectory)
{
    constexpr std::string_view fodt = R"xml(<?xml version="1.0" encoding="UTF-8"?>
<office:document xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0" xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0" office:version="1.3" office:mimetype="application/vnd.oasis.opendocument.text">
  <office:automatic-styles>
    <style:page-layout style:name="DeclaredFirst">
      <style:page-layout-properties fo:page-width="10in" fo:page-height="10in" fo:margin="0.25in"/>
    </style:page-layout>
    <style:page-layout style:name="Selected">
      <style:page-layout-properties fo:page-width="8.5in" fo:page-height="11in" fo:margin="0.5in"/>
    </style:page-layout>
    <style:style style:name="AppliedBase" style:family="paragraph" style:master-page-name="SecondMaster"/>
    <style:style style:name="AppliedChild" style:family="paragraph" style:parent-style-name="AppliedBase"/>
  </office:automatic-styles>
  <office:master-styles>
    <style:master-page style:name="FirstMaster" style:page-layout-name="DeclaredFirst"/>
    <style:master-page style:name="SecondMaster" style:page-layout-name="Selected"/>
  </office:master-styles>
  <office:body><office:text><text:p text:style-name="AppliedChild">layout</text:p></office:text></office:body>
</office:document>)xml";
    const auto path = outputDirectory / "odf-master-page-layout.fodt";
    writeTextFile(path, fodt);

    const auto read = WordDocumentReader{}.read(path);
    printDiagnostics(read.diagnostics);
    expect(!read.hasErrors(), "a master-page-bound ODF layout is readable");
    expect(read.document.section().pageWidthTwips == 12'240
               && read.document.section().pageHeightTwips == 15'840,
           "the selected master page controls the imported page dimensions");
    expect(read.document.section().marginTopTwips == 720
               && read.document.section().marginRightTwips == 720
               && read.document.section().marginBottomTwips == 720
               && read.document.section().marginLeftTwips == 720,
           "the applied master page imports the fo:margin shorthand on all sides");
}

void verifyTableCellMasterPageIsIgnored(
    const std::filesystem::path& outputDirectory)
{
    constexpr std::string_view fodt = R"xml(<?xml version="1.0" encoding="UTF-8"?>
<office:document xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0" xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" xmlns:table="urn:oasis:names:tc:opendocument:xmlns:table:1.0" xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0" office:version="1.3" office:mimetype="application/vnd.oasis.opendocument.text">
  <office:automatic-styles>
    <style:page-layout style:name="DefaultLayout">
      <style:page-layout-properties fo:page-width="8.5in" fo:page-height="11in"/>
    </style:page-layout>
    <style:page-layout style:name="CellOnlyLayout">
      <style:page-layout-properties fo:page-width="11in" fo:page-height="8.5in"/>
    </style:page-layout>
    <style:style style:name="CellParagraph" style:family="paragraph" style:master-page-name="CellMaster"/>
  </office:automatic-styles>
  <office:master-styles>
    <style:master-page style:name="DefaultMaster" style:page-layout-name="DefaultLayout"/>
    <style:master-page style:name="CellMaster" style:page-layout-name="CellOnlyLayout"/>
  </office:master-styles>
  <office:body><office:text>
    <table:table><table:table-row><table:table-cell>
      <text:p text:style-name="CellParagraph">cell</text:p>
    </table:table-cell></table:table-row></table:table>
  </office:text></office:body>
</office:document>)xml";
    const auto path = outputDirectory / "odf-table-cell-master-page.fodt";
    writeTextFile(path, fodt);

    const auto read = WordDocumentReader{}.read(path);
    printDiagnostics(read.diagnostics);
    expect(!read.hasErrors(), "a table-cell master-page edge is readable");
    expect(read.document.section().pageWidthTwips == 12'240
               && read.document.section().pageHeightTwips == 15'840,
           "a paragraph inside a table does not switch the document master page");
}

} // namespace

int main()
{
    const std::filesystem::path outputDirectory{IIGENERALDOCUMENT_TEST_OUTPUT_DIR};
    std::filesystem::create_directories(outputDirectory);
    verifyStyleFamilies(outputDirectory);
    verifyPartScopedFontAliases(outputDirectory);
    verifyMasterPageLayoutSelection(outputDirectory);
    verifyTableCellMasterPageIsIgnored(outputDirectory);
}
