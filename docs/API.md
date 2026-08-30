# API

## Thinking Space document model

Include `ThinkingSpace/DocumentModel.h` for the complete structured-note
surface or include a domain header directly. The high-level persistence entry
points are `ThinkingSpace::NoteBodyPersistence`,
`ThinkingSpaceLocalNoteFileStore`, and `NoteEditorDocumentSession`.

`ThinkingSpace::NoteBodyPersistence` converts the editable source language to
and from the `THINKINGSPACENOTE` XML representation. Source constructs such as
styles, callouts, resources, links, and semantic tags remain editable rather
than being flattened into presentation HTML. `SetTag`, `SetProperty`, and
`GetProperty` expose the same typed editor mutation contract used by the source
application.

`ThinkingSpaceLocalNoteFileStore` creates, reads, updates, and deletes unpacked
`.tsnote` packages. Updates can capture `.tsnversion` snapshots and unified
header/body diffs through `ThinkingSpaceLocalNoteVersionStore`. Folder, tag,
library, and resource persistence types are available under their matching
`ThinkingSpace` subdirectories.

The authoritative format and provenance mapping is documented in
`THINKING_SPACE_DOCUMENT_MODEL.md`.

## Document model

`Document` owns pages, metadata, and terminal AcroForm fields. `Page` owns its media box, rotation, ordered elements, and annotations. Documents and pages are move-only so resource identity is not accidentally duplicated.

Every page element has:

- a stable, document-wide `ElementId`;
- an `ElementKind`;
- an ordered mutable `std::vector<PdfInstruction>`;
- optional bounds when they can be derived without guessing.

`PdfValue` represents null, boolean, integer, real, name, string bytes, array, dictionary, or preserved raw syntax. Arrays and dictionaries are recursive. `toPdfSyntax()` always escapes names and literal strings before writing.

## High-level editing

`DocumentEditor` allocates unique IDs and provides common operations:

```cpp
Document document;
document.addPage(Page(Rect{0, 0, 612, 792}));

DocumentEditor editor(document);
const auto text = editor.addText(0, "Hello", Point{72, 700}, 18);
const auto border = editor.addRectangle(0, Rect{60, 640, 200, 40});
editor.replaceText(text, 0, "Hello PDF");
editor.remove(border);
```

`find`, `replaceText`, `replaceImage`, and `remove` traverse recursive Form XObject contents as well as top-level page elements. Because shared forms use shared `FormContent`, one nested element has one identity even when painted multiple times.

For uncommon operations, edit `Element::instructions()` directly. The caller remains responsible for valid PDF operator arity and resource references; `DocumentValidator` checks structural invariants but is not a full ISO 32000 conformance checker.

## Images

An `ImageElement` separates placement instructions from `ImageInfo`. The reader exposes dimensions, color space, filter/decode-parameter syntax, encoded stream bytes, and source object reference. `replaceImage` accepts new bytes plus the complete image decoding contract.

Unfiltered 8-bit `/DeviceRGB` payloads must contain exactly `width * height * 3` bytes. Encoded data may specify filters such as `/DCTDecode`; the bytes must already match the declared filter.

## Annotations and forms

Annotations live outside page paint instructions. Their standard subtype, rectangle, contents, flags, and complete PDF dictionary are independently editable. Writing rebuilds the page annotation array, preserving indirect field/widget relationships contained in the dictionary.

Existing terminal AcroForm fields expose name, type, value, flags, choices, and source identity. Text and choice appearances are regenerated when QPDF supports the value. Non-ASCII values update the canonical field value and produce a warning because QPDF cannot reliably synthesize that appearance. Creating a new AcroForm/widget hierarchy is outside version 0.1.

Use `FormField::setUtf8Value()` for text and choice values. The writer encodes it as a PDF Unicode string. `setValue()` remains available for button names and other low-level PDF values.

## Results and diagnostics

Readers and writers return `ReadResult` and `WriteResult`. Check `hasErrors()` before using or delivering output. Non-fatal repair, appearance, and post-write warnings remain in `diagnostics` with stable codes and context strings.
