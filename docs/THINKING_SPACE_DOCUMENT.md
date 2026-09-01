# Thinking Space Document

## Implemented scope

Thinking Space Document uses the `.tsdoc` extension. The current model owns
header metadata, one HTML-block body, and an in-memory version object store. It
does not yet define a disk container, parser, writer, or migration policy.

The document composition has four parts:

- `ThinkingSpaceDocumentHeader` owns metadata independently from document
  content. Metadata is currently an ordered string key-value map; no required
  keys or value schema are fixed yet.
- `ThinkingSpaceDocumentBody` owns one `HtmlBlockDocument`. Its source remains
  the body source of truth, and every block-recognized custom tag is exposed as
  an independently addressable `HtmlBlock`.
- `ThinkingSpaceDocument` composes one header and one body and declares
  `.tsdoc` as its file extension.
- `ThinkingSpaceDocumentVersionHistory` owns content-addressed snapshots,
  reversible diff objects, and chronological version commits.

```cpp
ThinkingSpaceDocument document;
document.header.metadata["title"] = "Thinking Space";
document.body.htmlBlocks = HtmlBlockDocument::fromHtml(
    "<ts-paragraph style=\"display: block\">Text</ts-paragraph>");
const ThinkingSpaceDocumentVersion version = document.recordVersion(
    "Initial draft", "2026-09-01T00:00:00.000Z");
```

The body deliberately reuses the existing `HtmlBlockDocument` model and its
`iiHtmlBlock` adapter rather than introducing a second HTML range model. Custom
tags must satisfy that model's block-classification contract, such as an
explicit block display override. Header serialization and the physical
header/body boundary will be selected when `.tsdoc` I/O is implemented.

## Git object principle

The history follows Git's logical storage model rather than treating a patch
as the canonical version:

1. Header metadata and exact body HTML become independent `blob` objects.
2. A snapshot `tree` points to those two blob IDs.
3. A version `commit` points to its tree, parent commit, companion diff,
   timestamp, and label.
4. Every ID is SHA-256 over `type + " " + byte-size + NUL + payload`, so equal
   logical object content produces the same ID and retained objects can be
   verified without mutable sequence numbers.

Git itself treats snapshots as canonical and calculates diffs between trees.
Because `.tsdoc` explicitly requires a diff object, this model also stores one
content-addressed `ThinkingSpaceDocumentDiff` beside each version. It records
base/target snapshot IDs plus exact header/body byte deltas. A delta keeps the
common prefix and suffix and the removed/inserted middle bytes; `apply()`
rejects the wrong base and reconstructs the exact target for the correct base.
This may be coarser than a presentation-oriented multi-hunk diff, but it is
lossless and reversible.

`ThinkingSpaceDocumentDiff::apply(baseSnapshot)` applies both logical header
and body deltas, rebuilds blob/tree IDs, and returns a target only when the
computed tree matches `targetSnapshotObjectId`. A default empty snapshot is the
valid base for the first version.

The SHA-256 framing and parent/tree semantics are Git-style, but these objects
are not byte-compatible with a Git repository. Header canonicalization is an
internal hashing representation, not the future physical `.tsdoc` encoding.
Object IDs provide integrity and deduplication identity, not authentication or
a digital signature.

## Recording and retention

`recordVersion()` is the explicit commit boundary. Editing `header.metadata`
or `body.htmlBlocks` does not silently create a version. Callers may provide an
ISO-8601 UTC timestamp for deterministic imports/tests; an omitted timestamp is
generated from the current UTC time.

Versions are ordered oldest-to-newest and `head()` returns the newest one. The
history retains at most `maximumVersionCount == 100` commits. On overflow it:

- removes the oldest commits first;
- removes snapshot and diff objects that no retained commit references;
- increments `prunedVersionCount()`;
- preserves the first retained commit's missing parent ID in
  `shallowBoundaryParentObjectId()`.

The shallow boundary mirrors Git's shallow-history principle: surviving commit
IDs are never rewritten merely because an old ancestor was discarded.
`verifyIntegrity()` accepts that single declared boundary while checking every
remaining parent link, object hash, target snapshot, and reconstructable diff.

## Deferred contracts

This object model does not imply that a `.tsdoc` file can already be opened or
saved. The following decisions remain outside the current milestone:

- the physical envelope and header/body encoding;
- required metadata keys, typed values, and format versioning;
- custom-tag vocabulary and nesting rules;
- reader, writer, disk-level validation, atomic file commit, and compatibility
  behavior;
- branching, merging, authorship, signatures, and remote synchronization.

`iiGeneralDocument.ThinkingSpaceDocumentModel` verifies the aggregate surface,
`.tsdoc` extension, independent metadata/body composition, deterministic
content IDs, parent chains, reversible deltas, integrity checks, shallow
boundaries, oldest-first pruning, and the exact 100-version retention limit.
