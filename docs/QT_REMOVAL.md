# Standard C++23 document module (0.4)

Version 0.4 removes Qt from the entire document library, its public headers,
CMake packages, file access, author metadata, text/XML/JSON codecs, conversion
processes, tests and installed consumers. Both exported targets
`iiGeneralDocument::iiGeneralDocument` and `iiGeneralDocument::Pdf` are native
C++23 libraries. No Qt, iiXml or iiHtmlBlock binary is needed by either target.

## Dependency and ownership boundaries

| Responsibility | Implementation | Public boundary |
| --- | --- | --- |
| File access and atomic publication | `iiFileProvider::Native`, standard streams/filesystem with small POSIX/Win32 publication adapters | `std::filesystem::path`, byte strings/spans |
| Author snapshots and ledger | `iiFileProvider::Native`, nlohmann/json and utf8proc | Standard strings, maps and value objects |
| PDF interpretation/transformation | Private QPDF 12.3.2 with zlib 1.3.1 and libjpeg-turbo 3.2.0 | SDK document/page/element objects or `pdf::Document` |
| DOCX/ODT packages | Private libzip 1.11.4 | `WordDocument` |
| OOXML and OpenDocument XML | Private pugixml 1.16, standard Unicode/namespace validation | `WordDocument` |
| HTML and typed XML source ranges | Standard C++ parser logic adapted from first-party iiXml/iiHtmlBlock | Stable SDK block/node IDs |
| Thinking Space persistence | Private nlohmann/json 3.12.0; SHA-256 supplied by Native provider | Standard byte strings and SDK history objects |
| Legacy `.doc` conversion | Optional installed LibreOffice, isolated native process/profile with a timeout | `WordDocumentReader`/`WordDocumentWriter` |

The authored implementation uses C++23 without compiler language extensions.
Upstream zlib, libjpeg-turbo, libzip and utf8proc retain their original C sources;
these are open-source dependencies, not an extra authored application language.
POSIX/Win32 calls are confined to native file/process boundaries because standard
C++ does not specify atomic replacement on every platform or subprocess creation.
Qt frameworks, bindings, Python interpreters and commercial SDKs are not part of
the product runtime.

## Consumer migration

The standard-only provider is a separate package in the existing iiFileProvider
installation prefix. It shares the provider's staged-file lease coordination but
has no linkage to the legacy provider library or its object-store targets.

```cmake
find_package(iiGeneralDocument 0.4 CONFIG REQUIRED)
target_link_libraries(application PRIVATE iiGeneralDocument::iiGeneralDocument)
```

```cpp
#include <iiGeneralDocument/iiGeneralDocument.h>
#include <iiFileProviderNative.h>

auto author = iiFileProvider::native::FileAuthor::fromIisaccAccount(
    {{"sub", "document-author"}, {"email", "author@example.com"},
     {"displayName", "Document Author"}},
    "https://iisacc.com");
auto document = ii::document::HtmlBlockDocument::fromHtml("<p>Example</p>");
if (author) document.setFileAuthor(*author);
std::string bytes = document.toFileBytes();
iiFileProvider::native::File::write("example.html", bytes);
auto reopened = ii::document::HtmlBlockDocument::fromFileBytes(bytes);
```

`Document`, `WordDocument`, `HtmlBlockDocument`, `XmlTreeDocument` and
`ThinkingSpaceDocument` now expose `iiFileProvider::native::Authorship` and
`FileAuthor`. HTML, XML and Thinking Space `toFileBytes()` returns `std::string`;
`fromFileBytes()` accepts `std::string_view`. Byte strings can include NUL bytes.
The PDF byte-object component continues to use `std::vector<std::byte>`.
There are no QObject lifetimes, event-loop setup or Qt conversions in consumers.
Version 0.4 has a new shared-library ABI; clients of the full 0.3 API must rebuild.

## Existing file compatibility

Author profile schema 1 and author ledger schemas 1, 2 and 3 remain readable.
The complete profile is retained, including details, attribution, device data,
participants and named links. Reading a file never selects its author as the
current editing identity. HTML/XML Base64 metadata comments and Word/PDF embedded
metadata retain their established format and key.

Thinking Space schema 1 remains readable. New writes use schema 2: `removed` and
`inserted` in history diffs contain canonical Base64 because byte-prefix/suffix
diffs can cut through a multibyte UTF-8 character. Header/body text stays UTF-8.
SHA-256 object IDs are calculated from the same original bytes; upgrading a file
does not rewrite its stored snapshot, diff or commit IDs. The former schema-1
writer could fail on a valid Korean/emoji change whose diff was not itself UTF-8.
Schema 2 fixes that failure without changing history identity or the 100-version
retention contract. A 0.3 reader cannot open a newly written schema-2 `.tsdoc`.

Native XML decoding accepts UTF-8, BOM/recognized UTF-16 and UTF-32, and declared
ISO-8859-1. Invalid encodings, XML scalars, unknown/numeric entities, duplicate
expanded attribute names and unbound namespace prefixes fail closed. DTDs are
not externally loaded or resolved; the OpenDocument format layer rejects DTDs.
The source-preserving HTML/typed-XML model keeps its separate, relaxed markup
contract; it is not a W3C schema or browser DOM engine.

## Verification

`NativeDependencies` scans authored source/tests for Qt types and checks the full
macOS library's load commands. Staged and actual installed consumer builds disable
Qt, iiXml, iiHtmlBlock and the legacy iiFileProvider package, check exported target
interfaces, then execute document operations. Existing PDF, Word, ODF, HTML, XML,
authorship and history tests remain active. `NativeMigration` uses files generated
by the installed 0.3 library and compares full profile/ledger JSON and every history
ID. `CodecValidation` covers malformed entities/namespaces, Unicode byte diffs,
UTF-16 surrogates and canonical Base64. Permission tests require new Word files to
use ordinary `0666 & umask` modes and replacements to preserve existing modes.

Generated files are confined to `build/` and its subdirectories. macOS execution
is verified on the current host; Windows/Linux branches require their own native
toolchains and execution before platform-completion claims.
