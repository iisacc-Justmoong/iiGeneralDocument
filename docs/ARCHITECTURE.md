# Architecture

## Dependency direction

The dependency graph is intentionally one-way:

```text
Core <- Model <- Editing
  ^       ^         ^
  |       |         |
  +-------+---- IO interfaces
                 ^
                 |
             Pdf adapters

Model <- Validation

Word model <- Word reader/writer <- DOCX package adapter <- libzip + Qt XML
                         ^
                         |
                 legacy DOC converter <- isolated LibreOffice process

XML tree document <- XML tree editor <- iiXml TagDocument/range parser

HTML block document <- HTML block editor <- iiHtmlBlock range/serializer/delete

ThinkingSpace editor -> note session -> note package/store -> filesystem
        |                    |                 |
        +-> components       +-> version diff +-> hierarchy/resources/tags
```

`Core` owns diagnostics, PDF scalar/container values, and content instructions. `Model` owns documents and independently addressable elements. `Editing` performs use-case operations. `IO` contains abstract PDF-model reader and writer contracts. `Pdf` is the only layer that includes QPDF. `Validation` has no codec responsibility. `Xml` owns a strict source-preserving tree model and atomic subtree CRUD coordinator; only its implementation files translate iiXml node, range, declaration, and attribute types. `Html` owns an independent source-preserving block model and atomic CRUD coordinator; only its implementation files translate iiHtmlBlock range types. `Word` owns a separate flow-oriented model and public format dispatcher. Only `Word/Private` includes libzip or launches LibreOffice; those implementation types are not installed.

At the package boundary, `iiGeneralDocument` publicly links Qt Core, Qt Gui,
`iiXml::iiXml`, and `iiHtmlBlock::iiHtmlBlock`; Qt Qml is private. The Thinking
Space note model owns the markup dependency and never reverses into the PDF
`Core` or `Model` layers. The PDF model remains independent from Qt and the
structured-note model. The XML and HTML public APIs also avoid exposing dependency
objects even though iiXml and iiHtmlBlock remain public package dependencies. QPDF and libzip are privately linked and hidden from the
public ABI. LibreOffice is an optional runtime process dependency only for
legacy `.doc`; it is neither linked nor bundled.

The Thinking Space source layout preserves the source application's domain
boundaries. Editor commands depend on body persistence interfaces; local note
storage coordinates header/body/version components; hierarchy and resource
support remain below note/hub use cases. `ArchitecturePolicyLock` retains the
original runtime dependency checks. The QML view layer is deliberately outside
this library.

## SOLID boundaries

- SRP: each class has one change reason. For example, `TextElement` edits text operands, `DocumentEditor` locates and coordinates objects, `DocumentValidator` enforces invariants, and `PdfDocumentWriter` maps the model to a PDF file.
- OCP: a new PDF-page codec implements `DocumentReader` and `DocumentWriter`; a new semantic page type derives from `Element`. XML tree and HTML block recognition remain behind their document rebuild boundaries, while Word format dispatch depends on its flow model and its package/conversion adapters remain replaceable.
- LSP: every element preserves an ordered instruction sequence and can be consumed through `Element` without type-specific assumptions.
- ISP: reading and writing are separate interfaces. Applications that only inspect PDFs do not depend on mutation output.
- DIP: PDF editing and validation depend on the model and interfaces. XML CRUD depends on its public tree model while its implementation adapts iiXml; HTML CRUD similarly adapts iiHtmlBlock. Only outer format adapters depend on QPDF, libzip, or LibreOffice.

## Recognition strategy

The reader first parses every content object into `PdfInstruction`. It then partitions that complete ordered sequence into semantic elements:

- `BT ... ET` becomes a `TextElement`;
- path construction through its paint/end operator becomes a `PathElement`;
- image and Form XObject `Do` calls become resource-aware elements;
- `BI ... ID ... EI` becomes an `InlineImageElement`;
- shading, marked-content, and graphics-state operators receive dedicated types;
- any remaining operator or dangling operand becomes an `UnknownElement`.

No parsed instruction is intentionally discarded. The writer serializes the element order back to one page content stream. This is semantic preservation, not byte-for-byte preservation: whitespace, stream boundaries, compression, object numbers, and cross-reference layout may change.

Form XObjects are parsed recursively. Multiple placements of the same indirect form share one `FormContent`, reflecting PDF resource semantics. Editing a nested element therefore updates every placement that references that form. A future occurrence-specific edit should first clone/detach the form resource; it must not mutate the shared resource accidentally.

## Failure boundaries

Malformed values, duplicate IDs, invalid page geometry, invalid image payloads, and missing form resources fail validation or writing. Unknown page operators remain editable instructions. The original source bytes remain attached to a loaded document, allowing QPDF to preserve catalog structures that are not first-class model objects.

HTML mutations build a complete candidate source and block collection before
commit. Malformed or multi-root fragments, missing IDs, identity reconciliation
failures, and changes crossing iiXml overlay ranges leave source, IDs, next-ID
state, and revision untouched.

XML mutations use the same candidate-before-commit boundary with a stricter
hierarchy invariant. Multiple roots, content outside the root, malformed
prologs, cross-closed iiXml ranges, invalid fragments, and identity
reconciliation failures leave source, IDs, next-ID state, and revision
untouched. Removing the root intentionally enters an editable empty state; a
subsequent root create restores a complete XML document.

Signed files are rejected by default because any rewrite invalidates their signatures. Encrypted input is rejected on write by default because the current writer emits an unencrypted result. Both behaviors require explicit writer options to override.
