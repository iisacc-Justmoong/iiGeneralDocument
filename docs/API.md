# API

## Hierarchical XML documents

`XmlTreeDocument::fromXml()` adapts `iiXml::Parser::TagDocument` into a strict,
single-root hierarchy without exposing iiXml types in public headers. Every
`XmlNode` provides a runtime-stable ID, optional parent ID, ordered direct-child
IDs, hierarchy depth, name, inner/raw XML, exact opening and closing tag views,
attributes, self-closing state, and full-document byte ranges. `openingTag()`
includes attributes and the final `>`; `closingTag()` is empty for a
self-closing node. `XmlAttribute` retains iiXml's string, integer, real, and
boolean type inference.

`XmlTreeEditor` provides subtree-level CRUD:

```cpp
auto document = XmlTreeDocument::fromXml(
    "<catalog><item>Existing</item></catalog>");
XmlTreeEditor editor(document);

const XmlNodeId catalogId = *document.rootId();
const XmlNodeId created = editor.create(
    "<item enabled=true>Created</item>", catalogId);
const XmlNode* node = editor.read(created);
editor.update(created, "<entry>Updated</entry>");
editor.remove(created);
```

Create accepts exactly one bare element fragment and appends it as the last
child. An empty editor state accepts one new root; a non-empty document rejects
a second root. Creating below a self-closing parent expands it to a paired tag.
Update retains the selected root ID while replacing its descendants; remove
deletes the entire selected subtree. All source-changing mutations are parsed
and identity-reconciled in temporary state before one atomic commit. See
`XML_TREE_CRUD.md` for prolog handling, pointer lifetime, identity, and syntax
boundaries.

## HTML block documents

`HtmlBlockDocument::fromHtml()` uses `iiHtmlBlock` to recognize every block
element while preserving the complete source string. `rootIds()` exposes
top-level block roots in source order. Every `HtmlBlock` exposes a stable ID,
optional parent ID, ordered direct-child IDs, depth, exact opening and closing
tag views, self-closing state, tag, value, raw HTML, source ranges, and display
override metadata as an immutable view.

Parenthood is determined from the matching opening/closing ranges supplied by
iiHtmlBlock/iiXml: the nearest block range that fully contains a block is its
parent. Cross-closed overlay ranges that do not fully contain each other remain
independent roots instead of receiving a fabricated tree relationship.

`HtmlBlockEditor` provides block-level CRUD:

```cpp
auto document = HtmlBlockDocument::fromHtml(
    "<main><p>Existing</p></main>");
HtmlBlockEditor editor(document);

const HtmlBlockId mainId = document.blocks().front().id();
const HtmlBlockId created = editor.create("<p>Created</p>", mainId);
const HtmlBlock* block = editor.read(created);
editor.update(created, "<article>Updated</article>");
editor.remove(created);
```

Create appends at document level or as the last child of a supplied parent.
Create and update accept exactly one serialized block root. Update retains the
target ID; remove deletes the selected block and its nested identities. Every
successful mutation reparses temporary state and increments `revision()` once;
every failure rolls back completely. See `HTML_BLOCK_CRUD.md` for identity,
pointer lifetime, iiXml overlap, and accepted-syntax boundaries.

## Flow text documents

`WordDocument` is an ordered, flow-oriented model separate from the PDF page
model. Its `WordBlock` values are `WordParagraph` or `WordTable`. Paragraph
runs remain independently editable, including Unicode text, tabs, line breaks,
emphasis, separate Latin/East Asian fonts, size, and color. Page dimensions and
margins use Word twips. `WordParagraphProperties::numberingId` and
`numberingLevel` identify a list and its depth;
`numberingContinuation` marks an additional paragraph that belongs to the same
ODF list item rather than creating another numbered item.

`WordDocumentReader` and `WordDocumentWriter` select the backend from `.docx`,
`.doc`, `.odt`, or `.fodt`. DOCX, ODT, and FODT use native package/XML codecs.
DOC uses LibreOffice as an isolated conversion process; callers may specify its
executable and timeout in the matching options type. Every backend returns
stable diagnostics and `hasErrors()`. Reader and writer options both expose
`maximumXmlPartBytes`; the DOCX writer checks every generated XML/relationship
part and all native writers use it for pre-commit reopen validation, so a failed
validation never replaces an existing DOCX/ODT/FODT destination.

```cpp
WordDocument document;
WordParagraph paragraph;
paragraph.runs.push_back({"Hello Word", {.fontFamily = "Arial"}});
document.appendParagraph(std::move(paragraph));

auto result = WordDocumentWriter{}.write(document, "hello.docx");
auto reopened = WordDocumentReader{}.read("hello.docx");
auto odf = WordDocumentWriter{}.write(reopened.document, "hello.odt");
```

See `WORD_SUPPORT.md` for the supported object matrix, fidelity boundaries,
LibreOffice runtime contract, security limits, and verification gates. See
`ODF_SUPPORT.md` for ODT/FODT packaging, semantic mapping, and CRUD limits.

## Thinking Space documents

`ThinkingSpaceDocument` declares the in-memory object boundary for the
`.tsdoc` format. Its `ThinkingSpaceDocumentHeader` keeps string key-value
metadata separate from its `ThinkingSpaceDocumentBody`, which owns an existing
`HtmlBlockDocument` for independently addressable custom-tag blocks.

The current surface is declaration-only: no `.tsdoc` reader, writer, physical
envelope, required metadata schema, or custom-tag vocabulary is defined yet.
See `THINKING_SPACE_DOCUMENT.md` for the exact implemented and deferred
contracts.

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
