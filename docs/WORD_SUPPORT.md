# Word support contract

## Formats and backends

`WordDocumentReader` and `WordDocumentWriter` dispatch by the destination or
source extension:

| Format | Read | Write | Backend |
| --- | --- | --- | --- |
| `.docx` | Yes | Yes | Native OOXML/OPC implementation using Qt XML and privately linked libzip |
| `.doc` | Yes | Yes | Isolated LibreOffice conversion to or from the native DOCX codec |
| `.odt` | Yes | Yes | Native OpenDocument ZIP implementation using Qt XML and private libzip |
| `.fodt` | Yes | Yes | Native flat OpenDocument XML implementation using Qt XML |

DOCX is a ZIP package whose main document part is discovered through the OPC
package relationship rather than assumed to be at a fixed path. Legacy DOC is
the Word 97-2003 binary compound-file format. The library does not claim to
reimplement that binary specification; it requires a locally installed
`soffice`/`libreoffice` executable for `.doc`. Set
`libreOfficeExecutable` in `WordReadOptions` or `WordWriteOptions` when it is
not on `PATH`.

ODT and FODT use the same editable flow model without a LibreOffice runtime.
Their detailed ODF version, package, semantic mapping, security, and loss
contract is in `ODF_SUPPORT.md`. Both writers validate a same-directory
temporary result before atomic replacement and preserve the existing POSIX mode
when overwriting a document. A new native document uses the mode produced by a
same-directory POSIX `0666` creation probe, so the process umask is respected
without reading or changing it.

## Editable Word model

`WordDocument` is flow-oriented and independent from the page-coordinate PDF
model. It preserves ordered body blocks and exposes:

- core title, author, subject, description, keyword, creator, and modification
  metadata;
- paragraphs with style identifiers, alignment, numbering identity/level, and
  an ODF same-list-item continuation flag;
- independently editable text runs with tabs, line breaks, bold, italic,
  underline, Latin and East Asian font families, point size, and RGB color;
- tables with ordered rows, cells, and cell paragraphs;
- page width, height, and margins in Word twips.

New DOCX files contain explicit Normal, Title, Subtitle, and Heading 1-3
styles, exact section geometry, real numbering definitions, and fixed DXA table
geometry. The writer commits the ZIP atomically through libzip and reopens the
result before reporting success. Legacy DOC output is converted back to DOCX
and parsed again before success is reported.

## Explicit limits

- Images, drawings, embedded objects, headers, footers, comments, footnotes,
  endnotes, fields, content controls, and nested tables are not first-class
  model items in version 0.1.
- Hyperlink display text is read, but the hyperlink relationship is flattened
  and reported as a warning.
- Inserted tracked text is read as current content. Tracked deletions are
  excluded and reported as a warning.
- Unsupported body blocks and non-text run content produce diagnostics rather
  than a silent full-fidelity claim. Rewriting a source containing those items
  can omit them.
- Password-protected or encrypted packages are not supported.
- Each XML part is limited to 64 MiB by default. `WordReadOptions` applies the
  value while reading. Before archive commit, `WordWriteOptions` rejects every
  generated DOCX `.xml` and `.rels` part above the limit and applies the same
  limit to mandatory reopen validation. Override `maximumXmlPartBytes` only for
  a trusted document.
- LibreOffice conversion fidelity depends on the installed LibreOffice build
  and its Microsoft Word filters. A missing converter, timeout, conversion
  error, or unreadable converted output fails closed.

## Example

```cpp
#include <iiGeneralDocument/iiGeneralDocument.h>

using namespace ii::document;

WordDocument document;
document.metadata()["Title"] = "Word example";

WordParagraph paragraph;
paragraph.properties.styleId = "Heading1";
paragraph.runs.push_back({"Editable heading", {.bold = true}});
document.appendParagraph(std::move(paragraph));

auto written = WordDocumentWriter{}.write(document, "example.docx");
if (written.hasErrors()) {
    return 1;
}

auto read = WordDocumentReader{}.read("example.docx");
std::get<WordParagraph>(read.document.blocks().front()).runs.front().text =
    "Revised heading";
return WordDocumentWriter{}.write(read.document, "revised.docx").hasErrors();
```

## Verification

`iiGeneralDocument.WordModel` fixes the editable object contract.
`iiGeneralDocument.WordRoundTrip` writes, reopens, edits, and rewrites a DOCX
containing Unicode text, formatted runs, tabs, breaks, metadata, and a table.
`iiGeneralDocument.OdfRoundTrip`, `.OdfStyleCompatibility`, and `.OdfRobustness`
cover native ODT/FODT CRUD, external style/layout mapping, package integrity,
resource limits, and atomic overwrite behavior.
`iiGeneralDocument.LegacyDocRoundTrip` verifies the OLE compound-file signature
and performs a real LibreOffice DOC write/read cycle when `soffice` is present.
Release verification additionally renders the generated DOCX to page PNGs and
visually inspects every page.
