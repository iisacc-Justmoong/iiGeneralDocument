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

ThinkingSpace editor -> note session -> note package/store -> filesystem
        |                    |                 |
        +-> components       +-> version diff +-> hierarchy/resources/tags
```

`Core` owns diagnostics, PDF scalar/container values, and content instructions. `Model` owns documents and independently addressable elements. `Editing` performs use-case operations. `IO` contains abstract reader and writer contracts. `Pdf` is the only layer that includes QPDF. `Validation` has no codec responsibility.

At the package boundary, `iiGeneralDocument` publicly links Qt Core, Qt Gui,
`iiXml::iiXml`, and `iiHtmlBlock::iiHtmlBlock`; Qt Qml is private. The Thinking
Space note model owns the markup dependency and never reverses into the PDF
`Core` or `Model` layers. The PDF model remains independent from Qt and the
structured-note model.

The Thinking Space source layout preserves the source application's domain
boundaries. Editor commands depend on body persistence interfaces; local note
storage coordinates header/body/version components; hierarchy and resource
support remain below note/hub use cases. `ArchitecturePolicyLock` retains the
original runtime dependency checks. The QML view layer is deliberately outside
this library.

## SOLID boundaries

- SRP: each class has one change reason. For example, `TextElement` edits text operands, `DocumentEditor` locates and coordinates objects, `DocumentValidator` enforces invariants, and `PdfDocumentWriter` maps the model to a PDF file.
- OCP: a new codec implements `DocumentReader` and `DocumentWriter`; a new semantic page type derives from `Element`. Existing use cases need not depend on QPDF.
- LSP: every element preserves an ordered instruction sequence and can be consumed through `Element` without type-specific assumptions.
- ISP: reading and writing are separate interfaces. Applications that only inspect PDFs do not depend on mutation output.
- DIP: editing and validation depend on the model and interfaces. Only the outer PDF adapter depends on QPDF 12.3.2.

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

Signed files are rejected by default because any rewrite invalidates their signatures. Encrypted input is rejected on write by default because the current writer emits an unencrypted result. Both behaviors require explicit writer options to override.
