# Thinking Space document model

## Provenance and reproduction boundary

The Thinking Space document model reproduces the C++ document model from
`/Volumes/Storage/Workspace/Product/WhatSon` at Git snapshot
`b41eb16c3742c6f25d6b64edb8f6f8d58bdab509`, the parent of the first
`rm document model` commit. The dependency closure contains 157 colocated
headers and sources. The Android Storage Access Framework bridge is reproduced
from the same snapshot.

The snapshot references the tag hierarchy parser and store, but the source
repository's global `tags` ignore rule kept that directory outside Git. Those
eleven required tag hierarchy files were therefore recovered from the live
source workspace at `src/app/models/hierarchy/tags`. This is the only source
recovery outside the named commit; the contract test fixes the resulting file
count and all renamed format identifiers.

The source repository declares GNU AGPL 3.0. All Git-authored files in this
closure have one first-party author identity. This repository does not infer a
new distribution license from the transplant; a public distribution must carry
the license selected by the copyright holder.

## Rename contract

Only product and file-format naming changes inside the reproduced model:

| Source name | Thinking Space name |
| --- | --- |
| `WhatSon` | `ThinkingSpace` in C++ and Java identifiers |
| `WhatSon` | `Thinking Space` in user-facing text |
| `whatson` | `thinkingspace` in schemas, markers, package names, and logs |
| `WHATSON` | `THINKINGSPACE` in doctypes and environment variables |
| `ws*` / `Ws*` / `WS*` format prefixes | `ts*` / `Ts*` / `TS*` |

The resulting storage vocabulary includes:

- hub packages: `.tshub` with `.tscontents`;
- notes: `.tsnote`, `.tsnhead`, `.tsnbody`, `.tsnversion`, and `.tsnpaint`;
- resource packages and indexes: `.tsresource`, `.tsresources`;
- library and tag indexes: `Library.tslibrary/index.tsnindex` and
  `Tags.tstags`;
- schemas such as `thinkingspace.note.version.store` and
  `thinkingspace.resource.package`;
- body and header doctype `THINKINGSPACENOTE`.

The model contract test rejects obsolete product tokens and `.ws*` extensions
inside `src/ThinkingSpace` and the Android bridge.

## Model surface

`ThinkingSpace/DocumentModel.h` is the installed umbrella header. The
reproduced surface contains:

- source/body serialization, plain-text and HTML projection, semantic tags,
  web links, resources, callouts, breaks, and style components;
- `SetTag`, `SetProperty`, `GetProperty`, editor input filtering, clipboard
  import, and the active `NoteEditorDocumentSession`;
- note package creation, header parsing and creation, local CRUD, folder/tag
  bindings, statistics, hub mutation, and validators;
- snapshot capture, version-state encoding, unified diffs, checkout, rollback,
  and the 100-snapshot limit;
- library, resource, and tag hierarchy persistence;
- desktop/local filesystem behavior plus the Android Storage Access Framework
  bridge.

The original QML editor view is not part of this library model and is not
copied. `NoteActiveStateTracker` and the QML ownership calls used by the model
remain, so the target links Qt Core and Gui publicly and Qt Qml privately.
`iiXml` parses the note markup. `iiHtmlBlock` remains a direct public dependency
to preserve the source application's document dependency contract.

## Library integration adaptations

The reproduced implementation remains source-compatible apart from the rename
contract. Three package-boundary adaptations are necessary for a reusable
shared library:

- include paths move from `app/models/...` to `ThinkingSpace/...` while the
  original domain subdirectories remain intact;
- GCC/Clang visibility is opened only around Thinking Space public header
  declarations so the library's private QPDF ABI stays hidden;
- `AUTOMOC` builds the existing QObject-based model types.

The Java bridge is installed under
`share/iiGeneralDocument/platform/Android/src/com/iisacc/app/thinkingspace`.
An Android consumer must include that Java source in its package and provide
AndroidX `DocumentFile`, matching the original application integration.

## Verification

`iiGeneralDocument.ThinkingSpaceModel` exercises body/header round trips,
typed editor commands, local `.tsnote` persistence, version snapshots and
diffs, resource naming, and tag hierarchy persistence.
`iiGeneralDocument.ThinkingSpaceModelContract` verifies provenance, the
157-file closure, required Thinking Space schemas, and absence of obsolete
names. `iiGeneralDocument.InstallConsumer` compiles and executes an installed
consumer through the public umbrella header.
