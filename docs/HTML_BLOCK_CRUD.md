# HTML block CRUD support

## Scope

`HtmlBlockDocument` is an editable HTML/iiXml source model built on
`iiHtmlBlock 0.1.0`. The original source byte string is the document's source of
truth. `iiHtmlBlock::BlockRangeTracker` recognizes block elements and their
source ranges; the public iiGeneralDocument API converts those dependency types
into stable `HtmlBlockId` objects.

This module is intentionally separate from the PDF page model, the Word flow
model, and application persistence layers. It performs in-memory block CRUD.
The caller remains responsible for loading and persisting
`HtmlBlockDocument::html()` when a file or database is involved.

## Public model

`HtmlBlockDocument::fromHtml()` preserves the supplied source byte-for-byte and
exposes an ordered, read-only collection of `HtmlBlock` views. Each block
contains:

- a document-wide `HtmlBlockId`;
- an optional parent ID, ordered direct-child IDs, and hierarchy depth;
- its exact opening tag and matching closing tag, separated at the value
  boundaries reported by iiHtmlBlock/iiXml;
- its tag name, inner value, and complete raw HTML;
- full-document raw and value byte ranges;
- any `display` override recognized by `iiHtmlBlock`.

`HtmlBlockDocument::rootIds()` returns every top-level block ID in source order.
`openingTag()` includes attributes and the final `>`. `closingTag()` contains
the matched `</name>` markup, or is empty when `isSelfClosing()` is true.

An empty document and a source containing only an XML declaration, DOCTYPE, or
ASCII whitespace are valid and contain zero blocks. Parsing invalid markup
throws `DocumentError` without producing a partially initialized document.
IDs are stable only within one `HtmlBlockDocument` instance. They are not added
to the source or persisted; reloading HTML creates a new runtime identity set.

## CRUD contract

```cpp
#include <iiGeneralDocument/iiGeneralDocument.h>

using namespace ii::document;

auto document = HtmlBlockDocument::fromHtml(
    "<main><p>Existing</p></main>");
HtmlBlockEditor editor(document);

const HtmlBlockId mainId = document.blocks().front().id();
const HtmlBlockId created = editor.create("<p>Created</p>", mainId);
const HtmlBlock* read = editor.read(created);
editor.update(created, "<article>Updated</article>");
editor.remove(created);

const std::string& result = document.html();
```

`create(fragment)` appends one top-level block to the document.
`create(fragment, parentId)` appends it as the parent's last child. Leading and
trailing ASCII whitespace around a fragment is discarded. The fragment itself
must serialize through `iiHtmlBlock::BlockHTMLSerializer` as exactly one
top-level block; inline-only, malformed, or multiple-root fragments are
rejected.

`read(id)` returns a pointer to the current immutable block view or `nullptr`
for an unknown ID. A successful mutation rebuilds the view collection, so a
previously returned pointer must not be retained across `create`, `update`, or
`remove`.

`update(id, fragment)` replaces exactly the selected raw range. The selected
block keeps its ID even when the tag changes. Existing ancestors and unrelated
blocks also keep their IDs; replacement descendants receive new IDs. Updating
with the same serialized block HTML is a no-op that preserves every descendant
ID and does not increment the revision.

`remove(id)` uses the `iiHtmlBlock::DeleteBlock` result when that backend can
represent the remainder. Removing the last block is also supported, including
when only an XML declaration or DOCTYPE remains. Removing a parent removes all
of its descendant block identities. An unknown ID returns `false` without a
mutation.

## Atomicity and identity

Every source-changing edit is prepared in temporary source and block state,
reparsed, assigned stable IDs, rebuilt into roots and parent/child relations,
and only then committed. A successful source mutation increments `revision()`
exactly once. An identical update and a missing-ID remove are no-ops. Parse
failure, missing IDs, invalid fragments, range overflow, identity
reconciliation failure, and hierarchy rebuild failure leave source, blocks,
relationships, IDs, the next-ID sequence, and revision unchanged.

iiXml permits independently closed ranges that can overlap instead of forming a
strict tree. An operation that would cut through another overlapping block is
rejected rather than silently deleting or corrupting that block. Fully nested
descendants remain part of their selected parent CRUD unit. A parent is the
nearest opening-tag range whose matching close fully encloses the child's
matching close. Crossed ranges that do not contain each other are separate
roots and never receive a false parent ID.

## Syntax boundary

Block classification follows `iiHtmlBlock::DivideBlock`, including standard
block tags and explicit `display: block`, `flex`, `grid`, `table`, or related
block display values. This is the iiHtmlBlock/iiXml range grammar, not a browser
HTML5 error-recovery parser. Full namespace semantics, entity interpretation,
and semantic DOM nodes for CDATA and comments are outside the dependency's
current standards scope. Explicit self-closing markup is distinguished from a
paired opening/closing tag, but browser-style implicit closing and void-element
recovery are not performed. Callers that accept arbitrary web HTML should
normalize it with an HTML5 parser before creating this model.

## Verification

`iiGeneralDocument.HtmlBlockCrud` covers ordered roots, parent/child navigation,
depth, opening/closing tag boundaries, self-closing blocks, nested reads,
Unicode ranges, custom display blocks, top-level and child creation, stable IDs
across shifted ranges, hierarchy rebuild after update, recursive deletion,
final-root deletion, prefix preservation, invalid-fragment rollback, unknown
IDs, empty documents, and overlapping iiXml ranges.
`iiGeneralDocument.InstallConsumer` compiles and runs the same API from the
installed CMake package.
