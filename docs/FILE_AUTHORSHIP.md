# File authorship

iiGeneralDocument 0.2 requires iiFileProvider 0.2. It stores a shared `Authorship`
ledger: validated iisacc account/profile/device/attribution information, contributor
identifiers, first/latest change times, a latest-author reference and an exact
unsigned-decimal revision. The active editing identity is runtime context and is
never restored from a file. Tokens, login credentials and token refreshes never
enter this ledger. Metadata is attribution data, not verified ownership or login.

## Mutation boundaries

Call `setFileAuthor(iiFileProvider::FileAuthor)` on `Document`, `WordDocument`,
`HtmlBlockDocument`, `XmlTreeDocument` or `ThinkingSpaceDocument`. The selected
profile is recorded immediately. Selecting the identical profile changes only
runtime editing context. Successful changes regenerate the JSON dump before the
mutation returns, without a timer or a lazy getter. Changes before an author is
selected remain explicitly unattributed.

- PDF: `DocumentEditor` operations and `Document` page/field/version/metadata
  setters stamp edits. Identical text/image replacement and missing removal are
  no-ops. The cached JSON is also present in `metadata()["iisacc:authorship"]`.
- Word: paragraph/table insertion and `setMetadata` stamp directly. Use
  `WordDocument::edit(callback)` for nested run/style/table/section changes. It
  commits a copied draft, detects actual value changes and discards false/throw
  callbacks. An already stamped draft is not stamped twice.
- HTML/XML: successful subtree CRUD stamps at the existing atomic source/ID
  commit boundary. Invalid input and identical source leave metadata unchanged.
- Thinking Space: `edit(callback)` covers header and body changes and immediately
  synchronizes the header's reserved metadata key. Body HTML editors use the
  same ledger. `recordVersion` captures the current ledger into its snapshot.

Existing mutable vectors/maps and the public Thinking Space aggregate are kept
for source compatibility and detached construction. C++ cannot observe assignment
through retained raw references. Route authoring through the methods above;
legacy direct PDF/Word mutation must finish with `recordChange()`. Use
`ThinkingSpaceDocument::edit` for application edits so its header and body stay
synchronized. These document values have no file handle or hidden writable owner.
PDF/Word writers persist the already updated metadata when called; they are not
new background autosave services.

```cpp
word.setFileAuthor(author);
word.edit([](ii::document::WordDocument &draft) {
    std::get<ii::document::WordParagraph>(draft.blocks().at(0)).runs.at(0).text = "Revised";
    return true;
});
// word.authorship().dump() already contains the new revision and contribution.
auto result = ii::document::WordDocumentWriter{}.write(word, "revised.docx");
```

## Embedded formats

| Format | Internal representation |
| --- | --- |
| PDF | Unicode string under `/iisacc:authorship` in the document Info dictionary |
| DOCX | `docProps/custom.xml`, custom string property named `iisacc:authorship`, with package relationship and content type |
| ODT/FODT | `meta:user-defined` string property in document metadata |
| DOC | Existing LibreOffice bridge preserves the custom property through conversion |
| HTML/XML | `toFileBytes()` appends a base64 UTF-8 JSON comment; `fromFileBytes()` validates and removes that envelope before source parsing |
| `.tsdoc` | Schema 1 JSON container with header metadata, exact body HTML and the complete retained version/snapshot/diff store |

[OpenDocument 1.3, section 19.338](https://docs.oasis-open.org/office/OpenDocument/v1.3/os/part3-schema/OpenDocument-v1.3-os-part3-schema.html)
defines string as the default when `meta:value-type` is omitted. The reader
accepts that LibreOffice form and explicit string types. Other authored value
types, duplicate properties and malformed ledgers produce read errors. PDF/Word
writers use the sanitized ledger, so changing the reserved raw map value does not
inject arbitrary credential fields into authored output.

The markup envelope is `\n<!--iisacc:authorship:v1:BASE64-->` at the end of the
file. Standard base64 cannot introduce `--` into XML comments. Raw `html()` and
`xml()` remain the exact editable body, with no changing offsets. File users must
use `toFileBytes`/`fromFileBytes` to retain attribution. Malformed, duplicate or
future-version authored comments fail closed. External editors may remove custom
properties/comments, so preservation outside these tested routes is not assumed.

`.tsdoc` files have `format: "iiGeneralDocument.ThinkingSpace"`, `schemaVersion: 1`,
`header`, `bodyHtml`, and `history`. History stores versions, snapshots, diffs,
pruned count and shallow boundary. All version IDs and reversible diffs are
verified on read; retained/pruned history is preserved. The container is capped
at 64 MiB, with at most 100 retained versions, snapshots and diffs. Each author
ledger is capped by iiFileProvider at 1 MiB. There was no prior disk container;
unknown container versions are rejected instead of migrated implicitly.

## Dependency and validation

The requested iiFileProvider dependency is an actively maintained sibling under
AGPL-3.0-only, matching this SDK. It reuses Qt Core's JSON/value types and adds no
network calls, authentication SDK or storage engine. QPDF, libzip, Qt XML and the
existing LibreOffice bridge handle their existing file formats. The public model
layout changes require a rebuild and use SOVERSION 0.2.

Tests cover immediate dumps, no-op/rejected edits, PDF/DOCX/DOC/ODT/FODT and markup
round trips, LibreOffice's default string type, malformed/future metadata and
complete/pruned `.tsdoc` history. Standalone installed-package tests exercise the
same contract through exported CMake targets and headers.

Installed targets export non-system include directories so the selected SDK
prefix takes precedence over stale global `CPATH` headers. The installed consumer
regression test deliberately supplies a conflicting legacy umbrella header.
Thinking Space serialization rejects non-UTF-8 strings before writing JSON.
