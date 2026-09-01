set(required_files
    "README.md"
    "THIRD_PARTY_NOTICES.md"
    "docs/API.md"
    "docs/ARCHITECTURE.md"
    "docs/HTML_BLOCK_CRUD.md"
    "docs/ODF_SUPPORT.md"
    "docs/PDF_SUPPORT.md"
    "docs/THINKING_SPACE_DOCUMENT.md"
    "docs/WORD_SUPPORT.md"
    "docs/XML_TREE_CRUD.md"
    "src/iiGeneralDocument.h"
    "src/IO/DocumentReader.h"
    "src/IO/DocumentWriter.h"
    "src/Pdf/PdfDocumentReader.h"
    "src/Pdf/PdfDocumentWriter.h")
list(APPEND required_files
    "src/Html/HtmlBlockDocument.h"
    "src/Html/HtmlBlockEditor.h"
    "src/ThinkingSpace/ThinkingSpaceDocument.cpp"
    "src/ThinkingSpace/ThinkingSpaceDocument.h"
    "src/Word/WordDocument.h"
    "src/Word/WordDocumentReader.h"
    "src/Word/WordDocumentWriter.h"
    "src/Word/Private/AtomicFileCommit.cpp"
    "src/Word/Private/AtomicFileCommit.h"
    "src/Word/Private/OdfTextCodec.cpp"
    "tests/OdfRobustnessTest.cpp"
    "tests/OdfRoundTripTest.cpp"
    "tests/OdfStyleCompatibilityTest.cpp"
    "src/Xml/XmlTreeDocument.h"
    "src/Xml/XmlTreeEditor.h")

foreach(relative_path IN LISTS required_files)
    if(NOT EXISTS "${IIGENERALDOCUMENT_SOURCE_DIR}/${relative_path}")
        message(FATAL_ERROR "Required project file is missing: ${relative_path}")
    endif()
endforeach()

file(READ "${IIGENERALDOCUMENT_SOURCE_DIR}/src/Html/HtmlBlockDocument.h"
    html_document_header)
file(READ "${IIGENERALDOCUMENT_SOURCE_DIR}/src/Html/HtmlBlockEditor.h"
    html_editor_header)
if(html_document_header MATCHES "iiHtmlBlock"
        OR html_editor_header MATCHES "iiHtmlBlock")
    message(FATAL_ERROR
        "Public HTML CRUD headers must not expose iiHtmlBlock implementation types.")
endif()

file(READ "${IIGENERALDOCUMENT_SOURCE_DIR}/src/Xml/XmlTreeDocument.h"
    xml_document_header)
file(READ "${IIGENERALDOCUMENT_SOURCE_DIR}/src/Xml/XmlTreeEditor.h"
    xml_editor_header)
if(xml_document_header MATCHES "iiXml"
        OR xml_editor_header MATCHES "iiXml")
    message(FATAL_ERROR
        "Public XML CRUD headers must not expose iiXml implementation types.")
endif()

if(EXISTS "${IIGENERALDOCUMENT_SOURCE_DIR}/include")
    message(FATAL_ERROR "Public headers and sources must remain colocated; include/ is forbidden.")
endif()

file(READ "${IIGENERALDOCUMENT_SOURCE_DIR}/src/ThinkingSpace/ThinkingSpaceDocument.h"
    thinking_space_document_header)
foreach(required_text
        "maximumVersionCount{100}"
        "ThinkingSpaceDocumentDiff"
        "ThinkingSpaceDocumentVersionHistory"
        "recordVersion")
    string(FIND "${thinking_space_document_header}" "${required_text}" match_index)
    if(match_index EQUAL -1)
        message(FATAL_ERROR
            "Thinking Space version history contract is missing: ${required_text}")
    endif()
endforeach()

file(READ "${IIGENERALDOCUMENT_SOURCE_DIR}/src/Word/WordDocumentWriter.h"
    word_writer_header)
if(NOT word_writer_header MATCHES "maximumXmlPartBytes")
    message(FATAL_ERROR
        "WordWriteOptions must expose the pre-commit XML validation budget.")
endif()

file(READ "${IIGENERALDOCUMENT_SOURCE_DIR}/CMakeLists.txt" cmake_source)
foreach(required_text
        "IIGENERALDOCUMENT_QPDF_VERSION 12.3.2"
        "IIGENERALDOCUMENT_LIBZIP_VERSION 1.11.4"
        "find_package(iiXml 0.1.0 CONFIG REQUIRED)"
        "find_package(iiHtmlBlock 0.1.0 CONFIG REQUIRED)"
        "find_package(Qt6 6.5 REQUIRED COMPONENTS Core)"
        "iiXml::iiXml"
        "iiHtmlBlock::iiHtmlBlock"
        "iiGeneralDocument::iiGeneralDocument"
        "iiGeneralDocument.XmlTreeCrud"
        "iiGeneralDocument.HtmlBlockCrud"
        "iiGeneralDocument.ThinkingSpaceDocumentModel"
        "iiGeneralDocument.PdfRoundTrip"
        "iiGeneralDocument.WordModel"
        "iiGeneralDocument.WordRoundTrip"
        "iiGeneralDocument.OdfRoundTrip"
        "iiGeneralDocument.OdfStyleCompatibility"
        "iiGeneralDocument.OdfRobustness"
        "iiGeneralDocument.OdfLibreOfficeInterop"
        "iiGeneralDocument.LegacyDocRoundTrip")
    string(FIND "${cmake_source}" "${required_text}" match_index)
    if(match_index EQUAL -1)
        message(FATAL_ERROR "CMake contract text is missing: ${required_text}")
    endif()
endforeach()
