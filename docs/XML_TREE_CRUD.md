# Hierarchical XML CRUD support

## Scope

`XmlTreeDocument` is a source-preserving hierarchical XML model built on
`iiXml 0.1.0`. `iiXml::Parser::TagParser` supplies element ranges, hierarchy,
and typed attribute fields. The public adapter converts those implementation
types into stable `XmlNodeId`, `XmlNode`, and `XmlAttribute` values and adds a
strict single-root boundary suitable for subtree CRUD.

This module is independent from the PDF page model, HTML block model, Word
flow model, and Thinking Space persistence layer. It performs in-memory CRUD;
the caller owns file or database loading and persists `XmlTreeDocument::xml()`.

## Public model

`XmlTreeDocument::fromXml()` preserves the supplied bytes and exposes nodes in
preorder. Each `XmlNode` contains:

- a document-wide runtime `XmlNodeId`;
- an optional parent ID and ordered direct-child IDs;
- its element name, inner XML, complete raw XML, and self-closing state;
- full-document raw and value byte ranges;
- ordered attributes with name, optional value, inferred value type, and the
  iiXml declared-type flag.

The XML declaration, doctype, comments, processing instructions, root markup,
trailing XML whitespace, and UTF-8 BOM are preserved byte-for-byte. Both
iiXml's `<!Doctype root>` spelling and the standard `<!DOCTYPE root>` spelling
are accepted at the adapter boundary. A loaded document must contain exactly
one root. IDs live only inside one document instance and are not serialized;
reloading assigns a new runtime identity set.

## CRUD contract

```cpp
#include <iiGeneralDocument/iiGeneralDocument.h>

using namespace ii::document;

auto document = XmlTreeDocument::fromXml(
    "<catalog><item>Existing</item></catalog>");
XmlTreeEditor editor(document);

const XmlNodeId catalogId = *document.rootId();
const XmlNodeId created = editor.create(
    "<item enabled=true>Created</item>", catalogId);
const XmlNode* read = editor.read(created);
editor.update(created, "<entry>Updated</entry>");
editor.remove(created);

const std::string& result = document.xml();
```

`create(fragment, parentId)` trims outer ASCII whitespace, requires exactly one
bare element root, and appends the complete subtree as the parent's last child.
If the parent is self-closing, it is expanded to a paired tag while retaining
its ID. `create(fragment)` is accepted only when the document is in the empty
editor state and creates its sole root; it rejects a second document root.

`read(id)` returns a pointer to the current immutable node view or `nullptr` for
an unknown ID. A successful mutation rebuilds the view collection, so pointers
must not be retained across `create`, `update`, or `remove`.

`update(id, fragment)` replaces exactly one selected subtree. The replacement
root retains the selected ID even when its name changes. Existing ancestors and
unrelated nodes retain their IDs; old descendants disappear and replacement
descendants receive new IDs. An identical normalized replacement is a no-op
that preserves all IDs and does not increment `revision()`.

`remove(id)` deletes the selected raw range and all descendant identities. An
unknown ID returns `false`. Removing the root preserves its prolog and trailing
miscellaneous XML bytes and intentionally leaves an editable empty state. A new
root can then be created with `create(fragment)`.

## Hierarchy and atomicity

iiXml intentionally supports independently closed overlay ranges. A general
document XML tree does not: siblings must be ordered and non-overlapping, every
child range must be inside its parent's value range, and the document must have
one root with no non-miscellaneous content outside it. Cross-closed input such
as `<root><a><b></a></b></root>` is rejected.

Every source-changing operation constructs and parses temporary source, maps
stable identities by exact ranges, validates parent/child identity references,
and commits only after the complete candidate is valid. Parse errors, range
overflow, missing IDs, invalid fragments, duplicate identities, and hierarchy
violations leave source, nodes, next-ID state, and revision unchanged. A
successful source mutation increments `revision()` exactly once. Deleted IDs
are never reused within the document instance.

## Syntax boundary

Element recognition, self-closing tags, comments, CDATA, processing
instructions, tag names, relaxed attribute values, declared types, and value
type inference follow iiXml 0.1.0. This adapter validates hierarchy and prolog
placement but is not a W3C XML Schema, namespace, DTD, or entity-resolution
engine. It does not expand entities or validate a doctype against the root.
Applications that accept hostile or standards-sensitive XML should perform
schema, namespace, entity, and resource-access validation before constructing
domain objects from values.

## Verification

`iiGeneralDocument.XmlTreeCrud` covers prolog preservation, standard and iiXml
doctype spellings, Unicode ranges, hierarchy navigation, typed attributes,
root and subtree creation, self-closing parent expansion, stable IDs across
shifted ranges, subtree replacement, recursive deletion, root recreation,
invalid-source rejection, no-op updates, rollback, and non-reused IDs.
`iiGeneralDocument.ProjectContract` prevents iiXml implementation types from
leaking through public headers. `iiGeneralDocument.InstallConsumer` compiles
and runs the CRUD surface from the installed CMake package.
