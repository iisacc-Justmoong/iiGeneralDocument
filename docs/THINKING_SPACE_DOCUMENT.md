# Thinking Space Document

## Declaration scope

Thinking Space Document uses the `.tsdoc` extension. The current milestone
declares its in-memory object boundary only; it does not yet define a disk
container, parser, writer, validator, or migration policy.

The public model has three aggregate objects:

- `ThinkingSpaceDocumentHeader` owns metadata independently from document
  content. Metadata is currently an ordered string key-value map; no required
  keys or value schema are fixed yet.
- `ThinkingSpaceDocumentBody` owns one `HtmlBlockDocument`. Its source remains
  the body source of truth, and every block-recognized custom tag is exposed as
  an independently addressable `HtmlBlock`.
- `ThinkingSpaceDocument` composes one header and one body and declares
  `.tsdoc` as its file extension.

```cpp
ThinkingSpaceDocument document;
document.header.metadata["title"] = "Thinking Space";
document.body.htmlBlocks = HtmlBlockDocument::fromHtml(
    "<ts-paragraph style=\"display: block\">Text</ts-paragraph>");
```

The body deliberately reuses the existing `HtmlBlockDocument` model and its
`iiHtmlBlock` adapter rather than introducing a second HTML range model. Custom
tags must satisfy that model's block-classification contract, such as an
explicit block display override. Header serialization and the physical
header/body boundary will be selected when `.tsdoc` I/O is implemented.

## Deferred contracts

This declaration does not imply that a `.tsdoc` file can already be opened or
saved. The following decisions remain outside the current milestone:

- the physical envelope and header/body encoding;
- required metadata keys, typed values, and format versioning;
- custom-tag vocabulary and nesting rules;
- reader, writer, validation, atomic commit, and compatibility behavior.

`iiGeneralDocument.ThinkingSpaceDocumentModel` verifies the declared aggregate
surface, `.tsdoc` extension, independent metadata, and custom-tag HTML block
composition.
