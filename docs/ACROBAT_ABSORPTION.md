# Acrobat document-operation absorption (0.3)

The Acrobat engineering project established seven bounded PDF operation
contracts. They are now implemented by SDK-owned objects in this repository.
No SDK source, build target, installed header or runtime library loads the
Engineering project or an Adobe binary. This is an independent implementation
using QPDF, not recovered Adobe source code or a claim of full Acrobat parity.

## Object ownership

| Operation | Existing document model | Qt-free PDF component |
| --- | --- | --- |
| Structure inspection | `Document`, `Page`, `Annotation`, `FormField`, `Document::attachments()` | `pdf::Document` and its readonly page/field/attachment snapshots |
| Ordered page extraction, including repetitions | `DocumentEditor::extractPages()` returns a new `Document` | `pdf::Document::extractPages()` |
| Merge selected pages from multiple documents | `DocumentEditor::mergePages()` and `DocumentPageSelection` | `pdf::Document::merge()` and `pdf::PageSelection` |
| Relative batch rotation | `DocumentEditor::rotatePages()` | `pdf::Editor::rotatePages()` |
| Info metadata updates/removals | `setMetadataBatch()`, `Document::removeMetadata()` | `pdf::Editor::setMetadata()` |
| Unicode text notes | `addTextNote()`, `Annotation::contents()/author()/rect()` | `pdf::Editor::addTextNote()` |
| Existing text-field values | `setTextField()`, `FormField::utf8Value()/maximumLength()` | `pdf::Editor::setTextField()` |

All page indices are **zero based**, matching the existing SDK model. Extraction
requires a nonempty selection and retains order and repetitions. An empty
selection inside a merge item selects that entire document. Rotation requires
unique page indices and a multiple of 90 degrees; negative and large rotations
are normalized into `[0, 360)`.

Page copies retain page content, resources, boxes, rotation, UserUnit and
annotations. QPDF repairs copied AcroForm/widget ownership and makes conflicting
field names unique, including copies of the same page. New documents created
by extraction/merge do not copy document-level Info, XMP, attachments, outlines,
page labels or source authorship from every input. The aggregate adapter copies
the first/source document's authorship and active editing context, then records
one derived-document change. It never promotes file attribution into a current
authenticated identity. Source documents are not mutated.

An ordinary edit preserves the existing object graph, embedded attachments and
XMP. `Page::cropBox()` provides the effective box with a MediaBox fallback;
`Page::userUnit()` defaults to 1. `Annotation::author()` exposes `/T` text.
Attachment summaries describe embedded files but do not extract or mutate their
payloads. `PdfDocumentReader::readBytes()` and
`PdfDocumentWriter::writeBytes()` provide a standard byte boundary; the existing
path-based methods continue to use iiFileProvider for physical file CRUD.

Metadata batches accept Title, Author, Subject, Keywords, Creator and Producer,
with unique keys. A null optional removes a key. UTF-8 text rejects malformed
Unicode, embedded NUL and values larger than 1 MiB. New notes require nonempty
contents and finite, positive geometry in unrotated PDF coordinates.

Text-field editing requires one existing `/Tx` field. Missing/ambiguous names,
read-only fields, rich text, MaxLen-constrained fields and other field types are
rejected. This change does not create field/widget hierarchies. Unicode values
clear stale widget appearances and set NeedAppearances. The logical value is
verified after serialization; automatic Unicode font embedding and visual
appearance generation are not implemented. The existing aggregate writer
retains its ASCII appearance-generation path. Unchanged constrained fields can
be preserved; direct aggregate changes still undergo writer validation.

The native editor commits new bytes and snapshots only after serialization,
reopening and operation-specific postconditions succeed. New annotations keep
page references and widget appearance references through aggregate serialization;
indirect-reference values are parsed inside a single-object container. Page
assembly initializes the destination field cache before introducing copied
widgets, then repairs/registers them. Assembly/edit warnings reject the result
rather than publishing an apparently repaired object silently. Rejected edits leave
the object unchanged. Its snapshots are readonly and references expire after a
successful edit. The object owns a copy of its input bytes. Aggregate batch
edits validate the entire request before updating values/authorship. No-op
aggregate edits do not create a revision.

## Dependency boundary and consumption

Authored implementation and public interfaces use C++23. The `Pdf` CMake
component exposes only standard types and embeds pinned static OSS archives:
QPDF 12.3.2, zlib 1.3.1 and libjpeg-turbo 3.2.0. Archive and preserved license SHA-256 values and full
licenses are in `third_party/`. Builds of this component need no network, Qt,
iiFileProvider, iiXml, iiHtmlBlock, Adobe runtime or commercial SDK. QPDF C++
translation units use C++23; upstream C compression/JPEG/crypto files are built
unchanged as their original open-source libraries.

```cmake
find_package(iiGeneralDocument 0.4 CONFIG REQUIRED COMPONENTS Pdf)
target_link_libraries(application PRIVATE iiGeneralDocument::Pdf)
```

```cpp
#include <iiGeneralDocument/Pdf/Objects.h>
namespace pdf = ii::document::pdf;

auto loaded = pdf::Document::fromBytes(inputBytes);
if (!loaded) return 1;
pdf::Editor editor(*loaded);
const std::size_t pages[]{0};
if (!editor.rotatePages(pages, 90)) return 1;
auto outputBytes = loaded->toBytes();
```

Configure the isolated component with
`cmake -S . -B build/pdf-native -DIIGENERALDOCUMENT_PDF_ONLY=ON`.
`IIGENERALDOCUMENT_PDF_SANITIZERS=ON` instruments it and its private dependencies
for AddressSanitizer/UndefinedBehaviorSanitizer. Build and run CTest from the
same directory. All generated files stay below `build/`.

The original 0.3 absorption introduced the independent PDF component. Version
0.4 also migrates the entire `iiGeneralDocument::iiGeneralDocument` target to
standard C++23 author/file interfaces and open-source Word/XML/HTML codecs.
Both targets are Qt-free. The full ABI is now 0.4; full-target clients rebuild
against the new standard headers. See [the completed migration](QT_REMOVAL.md).

## Verification and limits

Tests use an independent xref-table PDF fixture with inherited geometry, two
pages, text content, AcroForm/widgets, an attachment and XMP. They exercise both
object APIs, saved/reopened results, repeated pages, name collisions, retained
content and catalog data, Unicode metadata/notes/fields, stale appearances,
atomic rejection, input ownership, signatures, encrypted files and XFA.
Installed consumers verify that the `Pdf` component works with Qt discovery
disabled and without private backend headers on its public interface.
Both package targets export their headers as ordinary include directories so a
stale global CPATH cannot take priority over the selected SDK headers. The
installed tests inject incompatible legacy headers to verify this ordering.
On macOS, runtime and symbol audits also reject Qt/Adobe/provider dynamic links
and public QPDF, zlib or JPEG backend symbols.

Signature detection includes ByteRange, nonempty signature values and DocMDP;
it does not validate cryptographic trust. Native manipulation refuses signed,
encrypted and XFA inputs. The legacy reader retains password/recovery options;
the legacy writer's existing explicit encryption/signature overrides remain,
while these new aggregate manipulation methods have no such override. Arbitrary
page-vector surgery is still rejected by that writer; use the extraction/merge
objects to obtain a new assembled source graph.

The engineering contracts are recorded in the adjacent Acrobat project's
`src/PdfOperations.cpp`, `tests/pdf_operations.cpp`,
`evidence/pdf-verification.json` and `evidence/pdf-independent-verification.json`.
The iisacc-authored MIT counterpart notice is retained under
`third_party/licenses/acrobat-counterpart/`. Adobe resources, binaries,
decompiler output and proprietary source were not copied into this SDK.


## Native dependency migration (0.4)

The complete module now uses C++23 standard types and open-source backends.
Qt and the installed iiXml/iiHtmlBlock packages are not runtime or public-package
dependencies. Author/file APIs use `iiFileProvider::Native`; Word XML uses pugixml
and persistence uses nlohmann/json. The markup algorithms retain the first-party
range/typed-attribute semantics described above. See [migration details](QT_REMOVAL.md)
for byte-string API changes, ABI compatibility, prior-file verification and the
Thinking Space schema-2 byte-diff encoding.
