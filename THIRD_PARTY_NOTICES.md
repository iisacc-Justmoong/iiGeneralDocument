# Dependency notices

The enclosing iiGeneralDocument SDK is MIT. All format dependencies below
are open source; full license texts are installed under
`share/licenses/iiGeneralDocument`. Qt is absent from the version-0.4 dependency
graph, including the full document target and its Native provider.

| Dependency | Version | License | Use |
| --- | --- | --- | --- |
| QPDF | 12.3.2 | Apache-2.0 (upstream notices also include Artistic-2.0) | PDF interpretation/transformation |
| zlib | 1.3.1 | zlib | PDF/ZIP compression |
| libjpeg-turbo | 3.2.0 | IJG, BSD-style, zlib | PDF DCT streams |
| libzip | 1.11.4 | BSD-3-Clause | DOCX/ODT ZIP packages |
| pugixml | 1.16 | MIT | Private Word XML codec |
| nlohmann/json | 3.12.0 | MIT | Private JSON persistence |
| utf8proc (Native provider) | 2.11.3 | MIT plus Unicode data terms | Author Unicode normalization |

QPDF, zlib and libjpeg-turbo release archives and licenses are recorded with
SHA-256 in `third_party/dependencies.json`. DOCX/ODT use pinned libzip commit
`6f8a0cdd24a0dc6cce9dac4a7679da784ab124ea`, or an explicitly requested system
libzip. Its crypto and optional compression/tool backends are disabled in the
pinned build. pugixml/nlohmann single-source files and licenses are recorded in
`third_party/document-dependencies.json`. Native provider dependency hashes and
licenses live under `iiFileProvider/Native/third_party` and are installed with
`iiFileProviderNative`. All private archive symbols are hidden from the SDK ABI.

First-party iiXml/iiHtmlBlock range, declaration, attribute and block-classification
algorithms were adapted into standard C++ private sources. Their original QObject
bindings/signals/debug logging are excluded. The source paths and original hashes
are recorded in `third_party/first-party-markup-provenance.json`; their MIT
license is included in `third_party/licenses/first-party-markup/LICENSE`.
Neither installed binary package is needed by iiGeneralDocument.

The PDF object codec and fixture adapt the iisacc-authored MIT engineering
counterpart; its full notice is preserved in
`third_party/licenses/acrobat-counterpart/LICENSE`. Native SHA-256 adapts the same
first-party standard implementation with its MIT notice in the provider's
`iisacc-counterpart/LICENSE`. No Adobe proprietary source, binary or extracted
resource is redistributed or required by these implementations.

Legacy `.doc` optionally launches user-installed LibreOffice. LibreOffice is not
linked or bundled with this SDK. Its distribution uses MPL-2.0/LGPL-3.0+ terms:
https://www.libreoffice.org/about-us/licenses/

Primary upstream projects:
https://github.com/qpdf/qpdf,
https://zlib.net/,
https://github.com/libjpeg-turbo/libjpeg-turbo,
https://libzip.org/,
https://pugixml.org/,
https://github.com/nlohmann/json,
https://github.com/JuliaStrings/utf8proc.
