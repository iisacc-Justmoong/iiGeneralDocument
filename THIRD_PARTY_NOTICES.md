# Third-party notices

## QPDF 12.3.2

QPDF is a content-preserving PDF transformation library. iiGeneralDocument pins commit `a898bb3a7289d1d05789d6d3f0d5dd534943a8da` from release 12.3.2 and builds only its static library.

- Project: https://github.com/qpdf/qpdf
- License: Apache License 2.0
- Copyright: 2005-2021 Jay Berkenbilt; 2022-2026 Jay Berkenbilt and Manfred Holger

The full license is available at https://github.com/qpdf/qpdf/blob/v12.3.2/LICENSE.txt.

## zlib

QPDF uses zlib for PDF stream compression and decompression. zlib is distributed under the zlib license: https://zlib.net/zlib_license.html.

## libzip 1.11.4

libzip reads, creates, and atomically commits the ZIP packages used by DOCX.
iiGeneralDocument pins commit `6f8a0cdd24a0dc6cce9dac4a7679da784ab124ea`
from release 1.11.4 and builds only its static library with optional crypto and
additional compression backends disabled.

- Project: https://libzip.org/
- License: BSD 3-Clause
- Copyright: 1999-2025 Dieter Baron and Thomas Klausner

The full license is installed with iiGeneralDocument when the pinned dependency
is used.

## LibreOffice

Legacy `.doc` support optionally invokes a user-installed LibreOffice process.
LibreOffice is not linked, redistributed, or downloaded by iiGeneralDocument.
The installed LibreOffice distribution and its licenses govern that runtime.

- Project: https://www.libreoffice.org/
- Licensing: Mozilla Public License 2.0 and GNU LGPL v3+ dual licensing

## libjpeg-turbo

QPDF uses the system JPEG implementation for DCT streams. The default build prefers an available static `libjpeg` archive for a self-contained iiGeneralDocument library and otherwise links the CMake `JPEG::JPEG` target. libjpeg-turbo carries IJG, BSD-style, and zlib licenses: https://github.com/libjpeg-turbo/libjpeg-turbo/blob/main/LICENSE.md.

## Qt Core, Gui, and Qml 6.8.3

The Thinking Space document model directly uses Qt Core, Gui, and Qml. `iiXml`
and `iiHtmlBlock` also link Qt Core. Qt is available under commercial and
open-source license options; the selected Qt distribution terms apply. The
locally installed Qt source includes the applicable LGPL, GPL, and commercial
license texts under the Qt source `LICENSES` directories.

- Project: https://www.qt.io/
- Licensing: https://www.qt.io/licensing/

## iiXml 0.1.0 and iiHtmlBlock 0.1.0

These first-party iisacc libraries are resolved as installed CMake packages rather than vendored into this repository. `iiHtmlBlock` depends on `iiXml`; iiGeneralDocument records both as direct public package dependencies. The XML tree adapter uses iiXml's declaration recognition, hierarchical `TagDocument` ranges, and typed attribute metadata. The HTML block CRUD adapter uses iiHtmlBlock's block classification, source-range tracking, fragment serialization, and deletion contracts.

- iiXml: https://github.com/iisacc-Justmoong/iiXml
- iiHtmlBlock: https://github.com/iisacc-Justmoong/iiHtmlBlock

## AndroidX DocumentFile

The optional Thinking Space Android Storage Access Framework bridge imports
`androidx.documentfile.provider.DocumentFile`. AndroidX is distributed under
the Apache License 2.0. The Java source is installed for Android consumers but
AndroidX binaries are not vendored by this repository.

- Project: https://developer.android.com/jetpack/androidx
- License: https://www.apache.org/licenses/LICENSE-2.0
