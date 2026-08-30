set(required_files
    "README.md"
    "THIRD_PARTY_NOTICES.md"
    "docs/API.md"
    "docs/ARCHITECTURE.md"
    "docs/PDF_SUPPORT.md"
    "docs/THINKING_SPACE_DOCUMENT_MODEL.md"
    "src/iiGeneralDocument.h"
    "src/ThinkingSpace/DocumentModel.h"
    "src/IO/DocumentReader.h"
    "src/IO/DocumentWriter.h"
    "src/Pdf/PdfDocumentReader.h"
    "src/Pdf/PdfDocumentWriter.h")

foreach(relative_path IN LISTS required_files)
    if(NOT EXISTS "${IIGENERALDOCUMENT_SOURCE_DIR}/${relative_path}")
        message(FATAL_ERROR "Required project file is missing: ${relative_path}")
    endif()
endforeach()

if(EXISTS "${IIGENERALDOCUMENT_SOURCE_DIR}/include")
    message(FATAL_ERROR "Public headers and sources must remain colocated; include/ is forbidden.")
endif()

file(READ "${IIGENERALDOCUMENT_SOURCE_DIR}/CMakeLists.txt" cmake_source)
foreach(required_text
        "IIGENERALDOCUMENT_QPDF_VERSION 12.3.2"
        "find_package(iiXml 0.1.0 CONFIG REQUIRED)"
        "find_package(iiHtmlBlock 0.1.0 CONFIG REQUIRED)"
        "find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml)"
        "iiXml::iiXml"
        "iiHtmlBlock::iiHtmlBlock"
        "IIGENERALDOCUMENT_THINKING_SPACE_SOURCES"
        "iiGeneralDocument::iiGeneralDocument"
        "iiGeneralDocument.ThinkingSpaceModel"
        "iiGeneralDocument.PdfRoundTrip")
    string(FIND "${cmake_source}" "${required_text}" match_index)
    if(match_index EQUAL -1)
        message(FATAL_ERROR "CMake contract text is missing: ${required_text}")
    endif()
endforeach()
