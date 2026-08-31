# iiGeneralDocument

`iiGeneralDocument` is a C++20 general document library. It provides separate,
editable models for PDF page content, hierarchical XML, HTML blocks, and
flow-oriented text documents while keeping format backends behind focused
boundaries.

The first release provides:

- ordered `Document`, `Page`, and stable `ElementId` objects;
- `TextElement`, `PathElement`, `ImageElement`, `FormXObjectElement`, `InlineImageElement`, `ShadingElement`, `MarkedContentElement`, `GraphicsStateElement`, and `UnknownElement`;
- recursive Form XObject contents, shared when the PDF resource is shared;
- separately editable annotations and existing AcroForm fields;
- lossless PDF value and content-instruction objects for low-level edits;
- reader and writer interfaces that do not depend on QPDF types;
- validation before writes, source-PDF preservation, and post-write reopening;
- fail-closed handling for digital signatures and encryption;
- source-preserving, iiXml-backed hierarchical XML CRUD with stable node IDs,
  parent/child navigation, typed attributes, subtree replacement, and strict
  single-root validation;
- source-preserving HTML/iiXml block CRUD with stable IDs, nested creation,
  atomic updates, recursive deletion, and overlap-safe range handling;
- a flow-oriented Word model plus native DOCX read/write for paragraphs,
  formatted runs, tables, numbering, metadata, and section geometry;
- native ODT and flat-XML FODT read/write through the same editable flow model,
  with standard package validation and atomic output;
- legacy Word 97-2003 `.doc` read/write through an isolated, timeout-bounded
  LibreOffice conversion backend with post-write reopening;

## Build

The build requires Qt Core 6.5 or newer plus the `iiXml` and `iiHtmlBlock`
0.1.0 CMake packages. Qt Core and both iisacc libraries are public package
dependencies, so an installed consumer receives the same include and link
contract. The standard installs at `~/.local/iiXml` and
`~/.local/iiHtmlBlock` are discovered automatically; set
`IIGENERALDOCUMENT_LOCAL_LIBRARY_ROOT` or `CMAKE_PREFIX_PATH` for another
installation root.

The default build fetches and statically links pinned QPDF 12.3.2 and libzip
1.11.4 sources in `build/`. Their tools, examples, tests, and unnecessary
compression or crypto backends are not added to the product build. Set
`IIGENERALDOCUMENT_USE_SYSTEM_QPDF=ON` or
`IIGENERALDOCUMENT_USE_SYSTEM_LIBZIP=ON` to require installed packages.
Legacy `.doc` support also needs a local LibreOffice executable at runtime;
native `.docx`, `.odt`, and `.fodt` support does not.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Use `-DIIGENERALDOCUMENT_USE_SYSTEM_QPDF=ON` to require an installed QPDF 12.3 package instead. Local builds use only `build/`; other build-directory names are unsupported.

## Example

```cpp
#include <iiGeneralDocument/iiGeneralDocument.h>

using namespace ii::document;

auto loaded = PdfDocumentReader{}.read("input.pdf");
if (loaded.hasErrors()) {
    return 1;
}

DocumentEditor editor(loaded.document);
for (const auto& page : loaded.document.pages()) {
    for (const auto& element : page.elements()) {
        if (const auto* text = dynamic_cast<const TextElement*>(element.get());
            text && !text->textSegments().empty()) {
            editor.replaceText(text->id(), 0, "Edited text");
            break;
        }
    }
}

const auto written = PdfDocumentWriter{}.write(loaded.document, "output.pdf");
return written.hasErrors() ? 1 : 0;
```

Text strings in a PDF content stream are encoded bytes, not universally Unicode text. `textSegments()` deliberately returns those bytes. A caller that changes text in a custom embedded font must encode the replacement for that font or replace the font resource as part of its document workflow. New documents receive a Helvetica/WinAnsi fallback for referenced font names.

HTML block documents use the `iiHtmlBlock` range and serialization contract:

```cpp
auto htmlDocument = HtmlBlockDocument::fromHtml(
    "<main><p>Existing</p></main>");
HtmlBlockEditor htmlEditor(htmlDocument);
const HtmlBlockId mainId = htmlDocument.blocks().front().id();
const HtmlBlockId created = htmlEditor.create("<p>Created</p>", mainId);
htmlEditor.update(created, "<article>Updated</article>");
htmlEditor.remove(created);
```

Hierarchical XML documents use `iiXml` ranges behind a dependency-free public
model:

```cpp
auto xmlDocument = XmlTreeDocument::fromXml(
    "<catalog><item>Existing</item></catalog>");
XmlTreeEditor xmlEditor(xmlDocument);
const XmlNodeId catalogId = *xmlDocument.rootId();
const XmlNodeId createdNode = xmlEditor.create(
    "<item enabled=true>Created</item>", catalogId);
xmlEditor.update(createdNode, "<entry>Updated</entry>");
xmlEditor.remove(createdNode);
```

Word and OpenDocument text files use the same flow model:

```cpp
WordDocument word;
WordParagraph heading;
heading.properties.styleId = "Heading1";
heading.runs.push_back({"Editable Word heading", {.bold = true}});
word.appendParagraph(std::move(heading));

const auto wordWritten = WordDocumentWriter{}.write(word, "output.docx");
auto wordRead = WordDocumentReader{}.read("output.docx");
const auto odtWritten = WordDocumentWriter{}.write(wordRead.document, "output.odt");
const auto fodtWritten = WordDocumentWriter{}.write(wordRead.document, "output.fodt");
auto flatOdfRead = WordDocumentReader{}.read("output.fodt");
```

See [API](docs/API.md), [architecture](docs/ARCHITECTURE.md), the [PDF support contract](docs/PDF_SUPPORT.md), the [XML tree CRUD contract](docs/XML_TREE_CRUD.md), the [HTML block CRUD contract](docs/HTML_BLOCK_CRUD.md), the [Word support contract](docs/WORD_SUPPORT.md), and the [OpenDocument support contract](docs/ODF_SUPPORT.md) before integrating editing into a product.
