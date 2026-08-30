# iiGeneralDocument

`iiGeneralDocument` is a C++20 general document library. It provides both a PDF object model for reading, editing, and writing page content without exposing the PDF backend and the reproduced Thinking Space structured-note model.

The first release provides:

- ordered `Document`, `Page`, and stable `ElementId` objects;
- `TextElement`, `PathElement`, `ImageElement`, `FormXObjectElement`, `InlineImageElement`, `ShadingElement`, `MarkedContentElement`, `GraphicsStateElement`, and `UnknownElement`;
- recursive Form XObject contents, shared when the PDF resource is shared;
- separately editable annotations and existing AcroForm fields;
- lossless PDF value and content-instruction objects for low-level edits;
- reader and writer interfaces that do not depend on QPDF types;
- validation before writes, source-PDF preservation, and post-write reopening;
- fail-closed handling for digital signatures and encryption;
- Thinking Space source/body editing, `.tsnote` package persistence,
  header/body parsing, resources, tag and folder bindings, and version diffs;
- the original Thinking Space editor command/session model under the
  `ThinkingSpace` C++ namespace and class prefix.

## Build

The build requires Qt 6.5 or newer with Core, Gui, and Qml plus the `iiXml` and `iiHtmlBlock` 0.1.0 CMake packages. Qt Core and Gui and both iisacc libraries are public package dependencies, so an installed consumer receives the same include and link contract. The standard installs at `~/.local/iiXml` and `~/.local/iiHtmlBlock` are discovered automatically; set `IIGENERALDOCUMENT_LOCAL_LIBRARY_ROOT` or `CMAKE_PREFIX_PATH` for another installation root.

The default build fetches and statically links the pinned QPDF 12.3.2 source in `build/`. QPDF's tools, examples, and tests are not added to the product build.

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

Thinking Space consumers may include the complete surface through:

```cpp
#include <iiGeneralDocument/ThinkingSpace/DocumentModel.h>

const QString body =
    ThinkingSpace::NoteBodyPersistence::serializeBodyDocument(
        QStringLiteral("note-id"), QStringLiteral("Hello Thinking Space"));
```

See [API](docs/API.md), [architecture](docs/ARCHITECTURE.md), the [PDF support contract](docs/PDF_SUPPORT.md), and the [Thinking Space document model contract](docs/THINKING_SPACE_DOCUMENT_MODEL.md) before integrating editing into a product.
