cmake_minimum_required(VERSION 3.31)
project(iiGeneralDocument VERSION 0.4.1 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(PROJECT_IS_TOP_LEVEL AND CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(_sdk_home "$ENV{HOME}")
    if(_sdk_home STREQUAL "")
        set(_sdk_home "$ENV{USERPROFILE}")
    endif()
    set(CMAKE_INSTALL_PREFIX "${_sdk_home}/.local/SDK/iiGeneralDocument" CACHE PATH
        "Installation prefix for iiGeneralDocument." FORCE)
    unset(_sdk_home)
endif()

include(CTest)
include(ExternalProject)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(GenerateExportHeader)

option(IIGENERALDOCUMENT_BUILD_SHARED "Build iiGeneralDocument as a shared library." ON)
option(IIGENERALDOCUMENT_USE_SYSTEM_LIBZIP "Use an installed libzip package instead of the pinned build dependency." OFF)

option(IIGENERALDOCUMENT_SANITIZERS "Instrument the full document module and tests" OFF)
if(IIGENERALDOCUMENT_SANITIZERS)
    if(MSVC)
        message(FATAL_ERROR "Document sanitizer validation requires Clang or GCC")
    endif()
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
    set(IIGENERALDOCUMENT_PDF_SANITIZERS ON CACHE BOOL "Instrument private PDF dependencies" FORCE)
endif()
option(IIGENERALDOCUMENT_PDF_ONLY "Build only the independent PDF object component." OFF)
option(IIGENERALDOCUMENT_PDF_SANITIZERS "Instrument the PDF component and its private dependencies." OFF)
include(cmake/PdfObjects.cmake)
if(IIGENERALDOCUMENT_PDF_ONLY)
    return()
endif()

set(IIGENERALDOCUMENT_LOCAL_LIBRARY_ROOT "$ENV{HOME}/.local/SDK" CACHE PATH
    "Root directory containing installed iisacc libraries.")
list(APPEND CMAKE_PREFIX_PATH "${IIGENERALDOCUMENT_LOCAL_LIBRARY_ROOT}/iiFileProvider")
include(cmake/DocumentDependencies.cmake)
find_package(iiFileProviderNative 0.5.0 CONFIG REQUIRED)
set(IIGENERALDOCUMENT_QPDF_TARGET iiGeneralDocumentPdfQpdf)
set(IIGENERALDOCUMENT_JPEG_TARGET iiGeneralDocumentPdfJpeg)

set(IIGENERALDOCUMENT_LIBZIP_TARGET "")
if(IIGENERALDOCUMENT_USE_SYSTEM_LIBZIP)
    find_package(libzip 1.11 CONFIG REQUIRED)
    if(NOT TARGET libzip::zip)
        message(FATAL_ERROR "The libzip package does not export libzip::zip.")
    endif()
    set(IIGENERALDOCUMENT_LIBZIP_TARGET libzip::zip)
else()
    set(IIGENERALDOCUMENT_LIBZIP_VERSION 1.11.4)
    set(IIGENERALDOCUMENT_LIBZIP_COMMIT 6f8a0cdd24a0dc6cce9dac4a7679da784ab124ea)
    set(IIGENERALDOCUMENT_LIBZIP_PREFIX "${CMAKE_BINARY_DIR}/_deps/libzip")
    set(IIGENERALDOCUMENT_LIBZIP_INSTALL "${IIGENERALDOCUMENT_LIBZIP_PREFIX}/install")
    file(MAKE_DIRECTORY "${IIGENERALDOCUMENT_LIBZIP_INSTALL}/${CMAKE_INSTALL_INCLUDEDIR}")

    ExternalProject_Add(iiGeneralDocumentLibzip
        PREFIX "${IIGENERALDOCUMENT_LIBZIP_PREFIX}"
        GIT_REPOSITORY https://github.com/nih-at/libzip.git
        GIT_TAG "${IIGENERALDOCUMENT_LIBZIP_COMMIT}"
        GIT_SHALLOW TRUE
        GIT_PROGRESS TRUE
        UPDATE_DISCONNECTED TRUE
        CMAKE_ARGS
            -DCMAKE_BUILD_TYPE=$<CONFIG>
            -DCMAKE_INSTALL_PREFIX=${IIGENERALDOCUMENT_LIBZIP_INSTALL}
            -DCMAKE_INSTALL_LIBDIR=${CMAKE_INSTALL_LIBDIR}
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON
            -DBUILD_SHARED_LIBS=OFF
            -DBUILD_TOOLS=OFF
            -DBUILD_REGRESS=OFF
            -DBUILD_OSSFUZZ=OFF
            -DBUILD_EXAMPLES=OFF
            -DBUILD_DOC=OFF
            -DENABLE_COMMONCRYPTO=OFF
            -DENABLE_GNUTLS=OFF
            -DENABLE_MBEDTLS=OFF
            -DENABLE_OPENSSL=OFF
            -DENABLE_WINDOWS_CRYPTO=OFF
            -DENABLE_BZIP2=OFF
            -DENABLE_LZMA=OFF
            -DENABLE_ZSTD=OFF
            -DCMAKE_C_FLAGS=${dep_flags}
        BUILD_BYPRODUCTS
            "${IIGENERALDOCUMENT_LIBZIP_INSTALL}/${CMAKE_INSTALL_LIBDIR}/${CMAKE_STATIC_LIBRARY_PREFIX}zip${CMAKE_STATIC_LIBRARY_SUFFIX}")

    if(APPLE)
        add_library(iiGeneralDocumentLibzipLibrary INTERFACE)
        target_include_directories(iiGeneralDocumentLibzipLibrary SYSTEM INTERFACE
            "${IIGENERALDOCUMENT_LIBZIP_INSTALL}/${CMAKE_INSTALL_INCLUDEDIR}")
        target_link_directories(iiGeneralDocumentLibzipLibrary INTERFACE
            "${IIGENERALDOCUMENT_LIBZIP_INSTALL}/${CMAKE_INSTALL_LIBDIR}")
        target_link_libraries(iiGeneralDocumentLibzipLibrary INTERFACE
            "-Wl,-hidden-lzip")
    else()
        add_library(iiGeneralDocumentLibzipLibrary STATIC IMPORTED GLOBAL)
        set_target_properties(iiGeneralDocumentLibzipLibrary PROPERTIES
            IMPORTED_LOCATION
                "${IIGENERALDOCUMENT_LIBZIP_INSTALL}/${CMAKE_INSTALL_LIBDIR}/${CMAKE_STATIC_LIBRARY_PREFIX}zip${CMAKE_STATIC_LIBRARY_SUFFIX}"
            INTERFACE_INCLUDE_DIRECTORIES
                "${IIGENERALDOCUMENT_LIBZIP_INSTALL}/${CMAKE_INSTALL_INCLUDEDIR}")
        if(UNIX)
            set_property(TARGET iiGeneralDocumentLibzipLibrary APPEND PROPERTY
                INTERFACE_LINK_OPTIONS "LINKER:--exclude-libs,libzip.a")
        endif()
    endif()
    add_dependencies(iiGeneralDocumentLibzipLibrary iiGeneralDocumentLibzip)
    set(IIGENERALDOCUMENT_LIBZIP_TARGET iiGeneralDocumentLibzipLibrary)
endif()

set(IIGENERALDOCUMENT_LIBRARY_TYPE STATIC)
if(IIGENERALDOCUMENT_BUILD_SHARED AND NOT EMSCRIPTEN)
    set(IIGENERALDOCUMENT_LIBRARY_TYPE SHARED)
endif()

add_library(iiGeneralDocument ${IIGENERALDOCUMENT_LIBRARY_TYPE}
    src/Core/Diagnostic.cpp
    src/Core/Diagnostic.h
    src/Core/PdfValue.cpp
    src/Core/PdfValue.h
    src/Editing/DocumentEditor.cpp
    src/Editing/DocumentEditor.h
    src/Html/HtmlBlockDocument.cpp
    src/Html/HtmlBlockDocument.h
    src/Html/HtmlBlockEditor.cpp
    src/Html/HtmlBlockEditor.h
    src/IO/DocumentReader.h
    src/IO/DocumentWriter.h
    src/Model/Document.cpp
    src/Model/Document.h
    src/Model/Element.cpp
    src/Model/Element.h
    src/Model/Geometry.h
    src/Model/Page.cpp
    src/Model/Page.h
    src/Pdf/PdfDocumentReader.cpp
    src/Pdf/PdfDocumentReader.h
    src/Pdf/PdfDocumentWriter.cpp
    src/Pdf/PdfDocumentWriter.h
    src/ThinkingSpace/ThinkingSpaceFile.cpp
    src/ThinkingSpace/ThinkingSpaceDocument.cpp
    src/ThinkingSpace/ThinkingSpaceDocument.h
    src/Word/WordDocument.cpp
    src/Word/WordDocument.h
    src/Word/WordDocumentReader.cpp
    src/Word/WordDocumentReader.h
    src/Word/WordDocumentWriter.cpp
    src/Word/WordDocumentWriter.h
    src/Word/Private/AtomicFileCommit.cpp
    src/Word/Private/AtomicFileCommit.h
    src/Word/Private/DocxPackage.cpp
    src/Word/Private/DocxPackage.h
    src/Word/Private/ProviderZipSource.h
    src/Word/Private/LegacyDocConverter.cpp
    src/Word/Private/LegacyDocConverter.h
    src/Word/Private/OdfTextCodec.cpp
    src/Word/Private/OdfTextCodec.h
    src/Xml/XmlTreeDocument.cpp
    src/Xml/XmlTreeDocument.h
    src/Xml/XmlTreeEditor.cpp
    src/Xml/XmlTreeEditor.h
    src/Validation/DocumentValidator.cpp
    src/Validation/DocumentValidator.h
    src/iiGeneralDocument.h
    src/Word/Private/ConversionProcess.cpp
    src/Codec/Private/Markup/Diagnostics.cpp
    src/Codec/Private/Markup/InlineProperties.cpp
    src/Codec/Private/Markup/OpenTag.cpp
    src/Codec/Private/Markup/Doctype.cpp
    src/Codec/Private/Markup/TagParser.cpp
    src/Codec/Private/Markup/GetTagInfo.cpp
    src/Codec/Private/Markup/DivideBlock.cpp
    src/Codec/Private/Markup/BlockRangeTracker.cpp
    third_party/pugixml/pugixml.cpp)
add_library(iiGeneralDocument::iiGeneralDocument ALIAS iiGeneralDocument)

generate_export_header(iiGeneralDocument
    EXPORT_FILE_NAME "${CMAKE_CURRENT_BINARY_DIR}/generated/iiGeneralDocument/Export.h")

target_compile_features(iiGeneralDocument PUBLIC cxx_std_23)
target_include_directories(iiGeneralDocument PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/iiGeneralDocument>)
target_include_directories(iiGeneralDocument PRIVATE third_party)
target_link_libraries(iiGeneralDocument
    PUBLIC
        iiFileProvider::Native
        iiGeneralDocumentPdf
    PRIVATE
        ${IIGENERALDOCUMENT_QPDF_TARGET}
        ${IIGENERALDOCUMENT_LIBZIP_TARGET}
        iiGeneralDocumentPdfZlib
        ${IIGENERALDOCUMENT_JPEG_TARGET})
if(APPLE)
    target_link_directories(iiGeneralDocument PRIVATE "${IIGENERALDOCUMENT_PDF_DEP_PREFIX}/lib")
    foreach(_archive IN ITEMS qpdf jpeg z)
        target_link_options(iiGeneralDocument PRIVATE "LINKER:-hidden-l${_archive}")
    endforeach()
elseif(UNIX)
    target_link_options(iiGeneralDocument PRIVATE "LINKER:--exclude-libs,ALL")
endif()
if(WIN32 AND IIGENERALDOCUMENT_USE_SYSTEM_LIBZIP)
    # Native qpdf packages may export zlib/jpeg as bare transitive names.
    # Resolve these alongside the imported qpdf archive, including consumers.
    target_link_directories(iiGeneralDocument PRIVATE
        "$<TARGET_FILE_DIR:${IIGENERALDOCUMENT_QPDF_TARGET}>")
endif()
if(MSVC)
    target_compile_options(iiGeneralDocument PRIVATE /W4)
else()
    target_compile_options(iiGeneralDocument PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()
set_target_properties(iiGeneralDocument PROPERTIES
    AUTOMOC OFF
    CXX_EXTENSIONS OFF
    EXPORT_NO_SYSTEM TRUE
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
    WINDOWS_EXPORT_ALL_SYMBOLS YES
    VERSION ${PROJECT_VERSION}
    SOVERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}")

install(TARGETS iiGeneralDocument
    EXPORT iiGeneralDocumentTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY src/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/iiGeneralDocument
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
    PATTERN "*/Private/*" EXCLUDE PATTERN "*_p.hpp" EXCLUDE)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/generated/iiGeneralDocument/Export.h"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/iiGeneralDocument)
install(EXPORT iiGeneralDocumentTargets
    FILE iiGeneralDocumentTargets.cmake
    NAMESPACE iiGeneralDocument::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/iiGeneralDocument)

configure_package_config_file(
    cmake/iiGeneralDocumentConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/iiGeneralDocumentConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/iiGeneralDocument)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/iiGeneralDocumentConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/iiGeneralDocumentConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/iiGeneralDocumentConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/iiGeneralDocument)
install(FILES docs/FILE_AUTHORSHIP.md DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/iiGeneralDocument)
install(FILES README.md THIRD_PARTY_NOTICES.md docs/API.md docs/ARCHITECTURE.md docs/PDF_SUPPORT.md
    docs/ACROBAT_ABSORPTION.md docs/HTML_BLOCK_CRUD.md docs/ODF_SUPPORT.md docs/WORD_SUPPORT.md
    docs/THINKING_SPACE_DOCUMENT.md docs/XML_TREE_CRUD.md
    DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/iiGeneralDocument)
if(NOT IIGENERALDOCUMENT_USE_SYSTEM_LIBZIP)
    install(FILES
        "${IIGENERALDOCUMENT_LIBZIP_PREFIX}/src/iiGeneralDocumentLibzip/LICENSE"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/iiGeneralDocument/libzip)
endif()

if(BUILD_TESTING)
    add_executable(iiGeneralDocumentPdfAbsorptionTest tests/PdfAbsorptionTest.cpp)
    target_link_libraries(iiGeneralDocumentPdfAbsorptionTest PRIVATE iiGeneralDocument::iiGeneralDocument)
    target_compile_definitions(iiGeneralDocumentPdfAbsorptionTest PRIVATE
        IIGENERALDOCUMENT_TEST_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}/test-output/acrobat-absorption")
    add_test(NAME iiGeneralDocument.PdfAbsorption COMMAND iiGeneralDocumentPdfAbsorptionTest)
    add_executable(iiGeneralDocumentXmlTreeCrudTest tests/XmlTreeCrudTest.cpp)
    target_link_libraries(iiGeneralDocumentXmlTreeCrudTest PRIVATE
        iiGeneralDocument::iiGeneralDocument)
    add_test(NAME iiGeneralDocument.XmlTreeCrud
        COMMAND iiGeneralDocumentXmlTreeCrudTest)

    add_executable(iiGeneralDocumentHtmlBlockCrudTest tests/HtmlBlockCrudTest.cpp)
    target_link_libraries(iiGeneralDocumentHtmlBlockCrudTest PRIVATE
        iiGeneralDocument::iiGeneralDocument)
    add_test(NAME iiGeneralDocument.HtmlBlockCrud
        COMMAND iiGeneralDocumentHtmlBlockCrudTest)

    add_executable(iiGeneralDocumentThinkingSpaceDocumentModelTest
        tests/ThinkingSpaceDocumentModelTest.cpp)
    target_link_libraries(iiGeneralDocumentThinkingSpaceDocumentModelTest PRIVATE
        iiGeneralDocument::iiGeneralDocument)
    add_test(NAME iiGeneralDocument.ThinkingSpaceDocumentModel
        COMMAND iiGeneralDocumentThinkingSpaceDocumentModelTest)

    add_executable(iiGeneralDocumentWordModelTest tests/WordModelTest.cpp)
    target_link_libraries(iiGeneralDocumentWordModelTest PRIVATE iiGeneralDocument::iiGeneralDocument)
    add_test(NAME iiGeneralDocument.WordModel COMMAND iiGeneralDocumentWordModelTest)

    add_executable(iiGeneralDocumentWordRoundTripTest tests/WordRoundTripTest.cpp)
    target_link_libraries(iiGeneralDocumentWordRoundTripTest PRIVATE iiGeneralDocument::iiGeneralDocument)
    target_compile_definitions(iiGeneralDocumentWordRoundTripTest PRIVATE
        IIGENERALDOCUMENT_TEST_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}/test-output")
    add_test(NAME iiGeneralDocument.WordRoundTrip COMMAND iiGeneralDocumentWordRoundTripTest)

    add_executable(iiGeneralDocumentOdfRoundTripTest tests/OdfRoundTripTest.cpp)
    target_link_libraries(iiGeneralDocumentOdfRoundTripTest PRIVATE
        iiGeneralDocument::iiGeneralDocument)
    target_compile_definitions(iiGeneralDocumentOdfRoundTripTest PRIVATE
        IIGENERALDOCUMENT_TEST_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}/test-output")
    add_test(NAME iiGeneralDocument.OdfRoundTrip COMMAND iiGeneralDocumentOdfRoundTripTest)

    add_executable(iiGeneralDocumentOdfStyleCompatibilityTest
        tests/OdfStyleCompatibilityTest.cpp)
    target_link_libraries(iiGeneralDocumentOdfStyleCompatibilityTest PRIVATE
        iiGeneralDocument::iiGeneralDocument
        ${IIGENERALDOCUMENT_LIBZIP_TARGET}
        iiGeneralDocumentPdfZlib)
    target_compile_definitions(iiGeneralDocumentOdfStyleCompatibilityTest PRIVATE
        IIGENERALDOCUMENT_TEST_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}/test-output")
    add_test(NAME iiGeneralDocument.OdfStyleCompatibility
        COMMAND iiGeneralDocumentOdfStyleCompatibilityTest)

    add_executable(iiGeneralDocumentOdfRobustnessTest
        tests/OdfRobustnessTest.cpp)
    target_link_libraries(iiGeneralDocumentOdfRobustnessTest PRIVATE
        iiGeneralDocument::iiGeneralDocument)
    target_compile_definitions(iiGeneralDocumentOdfRobustnessTest PRIVATE
        IIGENERALDOCUMENT_TEST_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}/test-output")
    add_test(NAME iiGeneralDocument.OdfRobustness
        COMMAND iiGeneralDocumentOdfRobustnessTest)

    find_program(IIGENERALDOCUMENT_TEST_SOFFICE_EXECUTABLE NAMES soffice libreoffice)
    if(IIGENERALDOCUMENT_TEST_SOFFICE_EXECUTABLE)
        add_executable(iiGeneralDocumentLegacyDocRoundTripTest
            tests/LegacyDocRoundTripTest.cpp)
        target_link_libraries(iiGeneralDocumentLegacyDocRoundTripTest PRIVATE
            iiGeneralDocument::iiGeneralDocument)
        target_compile_definitions(iiGeneralDocumentLegacyDocRoundTripTest PRIVATE
            IIGENERALDOCUMENT_TEST_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}/test-output"
            IIGENERALDOCUMENT_TEST_SOFFICE="${IIGENERALDOCUMENT_TEST_SOFFICE_EXECUTABLE}")
        add_test(NAME iiGeneralDocument.LegacyDocRoundTrip
            COMMAND iiGeneralDocumentLegacyDocRoundTripTest)

        add_executable(iiGeneralDocumentOdfLibreOfficeInteropTest
            tests/OdfLibreOfficeInteropTest.cpp src/Word/Private/ConversionProcess.cpp)
        target_link_libraries(iiGeneralDocumentOdfLibreOfficeInteropTest PRIVATE
            iiGeneralDocument::iiGeneralDocument)
        target_compile_definitions(iiGeneralDocumentOdfLibreOfficeInteropTest PRIVATE
            IIGENERALDOCUMENT_TEST_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}/test-output"
            IIGENERALDOCUMENT_TEST_SOFFICE="${IIGENERALDOCUMENT_TEST_SOFFICE_EXECUTABLE}")
        add_test(NAME iiGeneralDocument.OdfLibreOfficeInterop
            COMMAND iiGeneralDocumentOdfLibreOfficeInteropTest)
    endif()

    add_executable(iiGeneralDocumentModelTest tests/ModelTest.cpp)
    target_link_libraries(iiGeneralDocumentModelTest PRIVATE iiGeneralDocument::iiGeneralDocument)
    add_test(NAME iiGeneralDocument.Model COMMAND iiGeneralDocumentModelTest)

    add_executable(iiGeneralDocumentEditorTest tests/DocumentEditorTest.cpp)
    target_link_libraries(iiGeneralDocumentEditorTest PRIVATE iiGeneralDocument::iiGeneralDocument)
    add_test(NAME iiGeneralDocument.Editor COMMAND iiGeneralDocumentEditorTest)

    add_executable(iiGeneralDocumentPdfRoundTripTest tests/PdfRoundTripTest.cpp)
    target_link_libraries(iiGeneralDocumentPdfRoundTripTest PRIVATE iiGeneralDocument::iiGeneralDocument)
    target_compile_definitions(iiGeneralDocumentPdfRoundTripTest PRIVATE
        IIGENERALDOCUMENT_TEST_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}/test-output")
    add_test(NAME iiGeneralDocument.PdfRoundTrip COMMAND iiGeneralDocumentPdfRoundTripTest)

    add_executable(iiGeneralDocumentValidationTest tests/ValidationTest.cpp)
    target_link_libraries(iiGeneralDocumentValidationTest PRIVATE iiGeneralDocument::iiGeneralDocument)
    add_test(NAME iiGeneralDocument.Validation COMMAND iiGeneralDocumentValidationTest)

    add_test(NAME iiGeneralDocument.ProjectContract
        COMMAND ${CMAKE_COMMAND}
            -DIIGENERALDOCUMENT_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/ProjectContractTest.cmake)

    if(NOT IIGENERALDOCUMENT_SANITIZERS)
    add_test(NAME iiGeneralDocument.InstallConsumer
        COMMAND ${CMAKE_COMMAND}
            "-DIIGENERALDOCUMENT_CONFIG=$<CONFIG>"
            -DIIGENERALDOCUMENT_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DIIGENERALDOCUMENT_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -DIIGENERALDOCUMENT_CMAKE_COMMAND=${CMAKE_COMMAND}
            "-DIIGENERALDOCUMENT_NATIVE_PROVIDER_DIR=${iiFileProviderNative_DIR}"
            "-DIIGENERALDOCUMENT_NATIVE_PROVIDER_LIBRARY_DIR=$<TARGET_FILE_DIR:iiFileProvider::Native>"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/InstallConsumerTest.cmake)
    endif()

    if(UNIX)
        add_test(NAME iiGeneralDocument.SymbolIsolation
            COMMAND ${CMAKE_COMMAND}
                -DIIGENERALDOCUMENT_LIBRARY=$<TARGET_FILE:iiGeneralDocument>
                -DIIGENERALDOCUMENT_NM=${CMAKE_NM}
                -DIIGENERALDOCUMENT_APPLE=${APPLE}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/SymbolIsolationTest.cmake)
    endif()
endif()

if(BUILD_TESTING)
    add_executable(iiGeneralDocumentAuthorshipTest tests/AuthorshipTest.cpp)
    target_include_directories(iiGeneralDocumentAuthorshipTest PRIVATE third_party)
    target_link_libraries(iiGeneralDocumentAuthorshipTest PRIVATE iiGeneralDocument::iiGeneralDocument)
    add_test(NAME iiGeneralDocument.Authorship COMMAND iiGeneralDocumentAuthorshipTest)
endif()

if(BUILD_TESTING)
    add_test(NAME iiGeneralDocument.NativeDependencies COMMAND ${CMAKE_COMMAND}
        -DIIGENERALDOCUMENT_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -DIIGENERALDOCUMENT_LIBRARY=$<TARGET_FILE:iiGeneralDocument>
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/NativeDependenciesTest.cmake)
endif()
install(DIRECTORY third_party/licenses/ DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/iiGeneralDocument)
install(FILES third_party/document-dependencies.json DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/iiGeneralDocument)

if(BUILD_TESTING)
    add_executable(iiGeneralDocumentNativeMigrationTest tests/NativeMigrationTest.cpp)
    target_link_libraries(iiGeneralDocumentNativeMigrationTest PRIVATE iiGeneralDocument::iiGeneralDocument)
    target_include_directories(iiGeneralDocumentNativeMigrationTest PRIVATE third_party)
    target_compile_definitions(iiGeneralDocumentNativeMigrationTest PRIVATE
        IIGENERALDOCUMENT_MIGRATION_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/native-upgrade")
    add_test(NAME iiGeneralDocument.NativeMigration COMMAND iiGeneralDocumentNativeMigrationTest)
endif()

if(BUILD_TESTING)
    add_executable(iiGeneralDocumentCodecValidationTest tests/CodecValidationTest.cpp third_party/pugixml/pugixml.cpp)
    target_link_libraries(iiGeneralDocumentCodecValidationTest PRIVATE iiGeneralDocument::iiGeneralDocument)
    target_include_directories(iiGeneralDocumentCodecValidationTest PRIVATE third_party)
    add_test(NAME iiGeneralDocument.CodecValidation COMMAND iiGeneralDocumentCodecValidationTest)
endif()

install(FILES docs/QT_REMOVAL.md DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/iiGeneralDocument)

if(BUILD_TESTING)
    add_executable(iiGeneralDocumentConversionProcessTest tests/ConversionProcessTest.cpp src/Word/Private/ConversionProcess.cpp)
    target_link_libraries(iiGeneralDocumentConversionProcessTest PRIVATE iiGeneralDocument::iiGeneralDocument)
    add_test(NAME iiGeneralDocument.ConversionProcess COMMAND iiGeneralDocumentConversionProcessTest)
endif()

install(FILES third_party/first-party-markup-provenance.json DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/iiGeneralDocument)
