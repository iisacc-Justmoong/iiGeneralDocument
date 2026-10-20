# iiGeneralDocument

[Windows 빌드 및 실행](docs/WindowsBuildAndRun.md)

The Windows legacy DOC integration test allows two minutes per cold, isolated
LibreOffice profile. Applications retain control of `conversionTimeout`; its
default is unchanged. The console launcher is preferred for captured conversion
results on Windows.

Package release: **0.4.1** (2026-10-19). This patch packages the current
source snapshot; build, test and installation evidence is recorded in the SDK
patch-release report. Earlier versioned feature notes remain historical.


`iiGeneralDocument` is a native C++23 document library with standard public types
and open-source format backends. Version 0.4 removes Qt from the entire library,
including file access, author metadata, Word XML, JSON persistence, conversion
processes, tests and installed consumers.

It provides:

- editable PDF documents, pages, stable element IDs, content instructions,
  annotations, existing AcroForm fields and attachment summaries;
- ordered/repeated page extraction, selected-document merging, batch rotation,
  Unicode notes, metadata editing and existing text-field value editing;
- a standalone `iiGeneralDocument::Pdf` byte/object component with transactional
  edits, copied input bytes and C++23 standard types;
- source-preserving XML tree CRUD with typed attributes, parent/child navigation,
  subtree replacement and stable IDs;
- source-preserving HTML block CRUD with ordered roots, nested creation, display
  overrides, atomic updates and overlap-safe ranges;
- native DOCX, ODT and FODT read/write through one editable `WordDocument` flow
  model, including paragraphs, run styles, tables, numbering, metadata and geometry;
- optional legacy `.doc` read/write through an isolated, timeout-bounded
  LibreOffice process, with validation before atomic publication;
- standard author snapshots/ledgers with synchronous revision updates and embedded
  metadata in PDF, Word, HTML, XML and Thinking Space files;
- a `.tsdoc` aggregate with SHA-256 snapshots, parent links, byte diffs, integrity
  checks and newest-100 version retention; existing schema-1 files remain readable.

## Build and install

From `Workspace/SDK/iiGeneralDocument`, run `./install.sh`. It builds the
standard-only `iiFileProvider::Native` component, configures and tests the full
library in `build/`, installs to `~/.local/SDK/iiGeneralDocument`, and compiles/runs
an actual installed consumer with Qt and the legacy provider packages disabled.
No Qt kit is required. All generated files stay inside each repository's `build/`.

Direct configuration requires the Native provider package. The installer supplies
it from `Workspace/SDK/iiFileProvider/Native`; manual builds can install it first:

```sh
cmake -S ../iiFileProvider/Native -B ../iiFileProvider/build/native -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$HOME/.local/SDK/iiFileProvider"
cmake --build ../iiFileProvider/build/native --parallel 4
ctest --test-dir ../iiFileProvider/build/native --output-on-failure
cmake --install ../iiFileProvider/build/native
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_DISABLE_FIND_PACKAGE_Qt6=TRUE
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

The default top-level install prefix is `~/.local/SDK/iiGeneralDocument`; an
explicit `CMAKE_INSTALL_PREFIX` is respected. The installer accepts
`IIGENERALDOCUMENT_INSTALL_PREFIX` and `IIGENERALDOCUMENT_LOCAL_LIBRARY_ROOT`.
Installed packages discover the Native provider alongside the SDK or under that
SDK root. Imported targets also handle macOS runtime lookup when `LIBRARY_PATH`
makes package directories implicit to the linker.

## Consume the SDK

```cmake
find_package(iiGeneralDocument 0.4 CONFIG REQUIRED)
target_link_libraries(application PRIVATE iiGeneralDocument::iiGeneralDocument)
```

Include `<iiGeneralDocument/iiGeneralDocument.h>`. The full target publicly links
only `iiFileProvider::Native` and `iiGeneralDocument::Pdf`. Public author APIs use
`iiFileProvider::native`; HTML/XML/Thinking Space file bytes are `std::string` and
`std::string_view`. Version 0.4 is a new full-module ABI; existing clients rebuild.
See [migration and dependency boundaries](docs/QT_REMOVAL.md).

For the independent PDF byte/object component:

```cmake
find_package(iiGeneralDocument 0.4 CONFIG REQUIRED COMPONENTS Pdf)
target_link_libraries(application PRIVATE iiGeneralDocument::Pdf)
```

Include `<iiGeneralDocument/Pdf/Objects.h>`. A PDF-only build uses
`-DIIGENERALDOCUMENT_PDF_ONLY=ON` with a build directory such as
`build/pdf-native`. This component has no provider dependency.

## Backends and tests

Private backends are QPDF 12.3.2, zlib 1.3.1, libjpeg-turbo 3.2.0, libzip 1.11.4,
pugixml 1.16 and nlohmann/json 3.12.0. Native author normalization uses utf8proc
2.11.3. Release sources/licenses are pinned; local vendored files and archives are
SHA-256 checked during configuration. Upstream C libraries keep their own language;
authored library and test code is C++23 without compiler extensions. Optional
`IIGENERALDOCUMENT_USE_SYSTEM_LIBZIP=ON` uses an installed libzip package.
LibreOffice is needed only for legacy `.doc` conversion and interoperability tests.

The tests cover document mutation, format round trips, authorship, malformed input,
atomic writes/permissions, symbol isolation, installed package use, actual
LibreOffice interoperability, prior-file migration and absence of Qt dependencies.
Use `-DIIGENERALDOCUMENT_SANITIZERS=ON` for full AddressSanitizer/UndefinedBehaviorSanitizer
validation, or `IIGENERALDOCUMENT_PDF_SANITIZERS=ON` for the PDF component alone.
Build output and platform execution establish different guarantees: each target
platform still needs native execution before it is considered verified.

See [API](docs/API.md), [architecture](docs/ARCHITECTURE.md),
[PDF](docs/PDF_SUPPORT.md), [Word](docs/WORD_SUPPORT.md), [ODF](docs/ODF_SUPPORT.md),
[HTML](docs/HTML_BLOCK_CRUD.md), [XML](docs/XML_TREE_CRUD.md),
[Thinking Space](docs/THINKING_SPACE_DOCUMENT.md) and
[Acrobat feature absorption](docs/ACROBAT_ABSORPTION.md).

The enclosing SDK keeps its existing MIT license. Full dependency
licenses are shipped with the installation; see [notices](THIRD_PARTY_NOTICES.md).
No Adobe proprietary source, extracted resources or runtime is included.

MSVC builds link the bundled libjpeg-turbo archive as `jpeg-static.lib`, matching
the upstream Windows target name. The installed consumer test puts the package's
own `bin/` first on Windows to exercise the installed DLLs.
Both full-module and PDF-only installed consumers inherit the SDK build
configuration so MSVC runtime and standard-library layouts match the DLLs.
Windows legacy Word conversion prefers LibreOffice's console launcher
`soffice.com` so the native process boundary can capture completion and errors.

## License

SPDX-License-Identifier: MIT

The original code and documentation in this repository are licensed under the
[MIT License](LICENSE). Third-party code, libraries, fonts, tools, and model
weights retain their respective licenses.
