cmake_minimum_required(VERSION 3.31)
project(iiGeneralDocument VERSION 0.1.0 LANGUAGES C CXX)

include(CTest)
include(ExternalProject)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(GenerateExportHeader)

option(IIGENERALDOCUMENT_BUILD_SHARED "Build iiGeneralDocument as a shared library." ON)
option(IIGENERALDOCUMENT_USE_SYSTEM_QPDF "Use an installed qpdf package instead of the pinned build dependency." OFF)
option(IIGENERALDOCUMENT_USE_SYSTEM_LIBZIP "Use an installed libzip package instead of the pinned build dependency." OFF)

set(IIGENERALDOCUMENT_LOCAL_LIBRARY_ROOT "$ENV{HOME}/.local" CACHE PATH
    "Root directory containing installed iisacc libraries.")
foreach(package_name IN ITEMS iiXml iiHtmlBlock)
    set(package_prefix "${IIGENERALDOCUMENT_LOCAL_LIBRARY_ROOT}/${package_name}")
    if(EXISTS "${package_prefix}")
        list(APPEND CMAKE_PREFIX_PATH "${package_prefix}")
    endif()
endforeach()
unset(package_prefix)
unset(package_name)
if(CMAKE_PREFIX_PATH)
    list(REMOVE_DUPLICATES CMAKE_PREFIX_PATH)
endif()

find_package(iiXml 0.1.0 CONFIG REQUIRED)
find_package(iiHtmlBlock 0.1.0 CONFIG REQUIRED)
find_package(Qt6 6.5 REQUIRED COMPONENTS Core)
find_package(ZLIB REQUIRED)
find_package(JPEG REQUIRED)

if(APPLE AND TARGET WrapOpenGL::WrapOpenGL)
    get_target_property(iigeneraldocument_wrap_opengl_links
        WrapOpenGL::WrapOpenGL INTERFACE_LINK_LIBRARIES)
    if(iigeneraldocument_wrap_opengl_links)
        set(iigeneraldocument_filtered_wrap_opengl_links)
        foreach(iigeneraldocument_wrap_link IN LISTS iigeneraldocument_wrap_opengl_links)
            if(iigeneraldocument_wrap_link STREQUAL "-framework AGL"
                    OR iigeneraldocument_wrap_link MATCHES "AGL\\.framework")
                continue()
            endif()
            list(APPEND iigeneraldocument_filtered_wrap_opengl_links
                "${iigeneraldocument_wrap_link}")
        endforeach()
        set_property(TARGET WrapOpenGL::WrapOpenGL PROPERTY
            INTERFACE_LINK_LIBRARIES "${iigeneraldocument_filtered_wrap_opengl_links}")
    endif()
endif()

find_library(IIGENERALDOCUMENT_STATIC_JPEG_LIBRARY
    NAMES "${CMAKE_STATIC_LIBRARY_PREFIX}jpeg${CMAKE_STATIC_LIBRARY_SUFFIX}")
if(IIGENERALDOCUMENT_STATIC_JPEG_LIBRARY)
    add_library(iiGeneralDocumentJpeg STATIC IMPORTED GLOBAL)
    set_target_properties(iiGeneralDocumentJpeg PROPERTIES
        IMPORTED_LOCATION "${IIGENERALDOCUMENT_STATIC_JPEG_LIBRARY}")
    set(IIGENERALDOCUMENT_JPEG_TARGET iiGeneralDocumentJpeg)
else()
    set(IIGENERALDOCUMENT_JPEG_TARGET JPEG::JPEG)
endif()

set(IIGENERALDOCUMENT_QPDF_TARGET "")
if(IIGENERALDOCUMENT_USE_SYSTEM_QPDF)
    find_package(qpdf 12.3 CONFIG REQUIRED)
    if(TARGET qpdf::libqpdf)
        set(IIGENERALDOCUMENT_QPDF_TARGET qpdf::libqpdf)
    elseif(TARGET qpdf::libqpdf_static)
        set(IIGENERALDOCUMENT_QPDF_TARGET qpdf::libqpdf_static)
    else()
        message(FATAL_ERROR "The qpdf package does not export a supported library target.")
    endif()
else()
    set(IIGENERALDOCUMENT_QPDF_VERSION 12.3.2)
    set(IIGENERALDOCUMENT_QPDF_COMMIT a898bb3a7289d1d05789d6d3f0d5dd534943a8da)
    set(IIGENERALDOCUMENT_QPDF_PREFIX "${CMAKE_BINARY_DIR}/_deps/qpdf")
    set(IIGENERALDOCUMENT_QPDF_INSTALL "${IIGENERALDOCUMENT_QPDF_PREFIX}/install")
    file(MAKE_DIRECTORY "${IIGENERALDOCUMENT_QPDF_INSTALL}/${CMAKE_INSTALL_INCLUDEDIR}")

    ExternalProject_Add(iiGeneralDocumentQpdf
        PREFIX "${IIGENERALDOCUMENT_QPDF_PREFIX}"
        GIT_REPOSITORY https://github.com/qpdf/qpdf.git
        GIT_TAG "${IIGENERALDOCUMENT_QPDF_COMMIT}"
        GIT_SHALLOW TRUE
        GIT_PROGRESS TRUE
        UPDATE_DISCONNECTED TRUE
        CMAKE_ARGS
            -DCMAKE_BUILD_TYPE=$<CONFIG>
            -DCMAKE_INSTALL_PREFIX=${IIGENERALDOCUMENT_QPDF_INSTALL}
            -DCMAKE_INSTALL_LIBDIR=${CMAKE_INSTALL_LIBDIR}
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON
            -DBUILD_SHARED_LIBS=OFF
            -DBUILD_STATIC_LIBS=ON
            -DBUILD_DOC=OFF
            -DINSTALL_MANUAL=OFF
            -DINSTALL_EXAMPLES=OFF
            -DINSTALL_PKGCONFIG=OFF
            -DINSTALL_CMAKE_PACKAGE=OFF
            -DUSE_IMPLICIT_CRYPTO=OFF
            -DREQUIRE_CRYPTO_NATIVE=ON
        BUILD_COMMAND
            ${CMAKE_COMMAND} --build <BINARY_DIR> --config $<CONFIG> --target libqpdf
        INSTALL_COMMAND
            ${CMAKE_COMMAND} --install <BINARY_DIR> --config $<CONFIG> --component dev
        BUILD_BYPRODUCTS
            "${IIGENERALDOCUMENT_QPDF_INSTALL}/${CMAKE_INSTALL_LIBDIR}/${CMAKE_STATIC_LIBRARY_PREFIX}qpdf${CMAKE_STATIC_LIBRARY_SUFFIX}")

    if(APPLE)
        # Apple ld's hidden archive form prevents the private QPDF ABI from
        # leaking through the iiGeneralDocument shared-library symbol table.
        add_library(iiGeneralDocumentQpdfLibrary INTERFACE)
        target_include_directories(iiGeneralDocumentQpdfLibrary INTERFACE
            "${IIGENERALDOCUMENT_QPDF_INSTALL}/${CMAKE_INSTALL_INCLUDEDIR}")
        target_link_directories(iiGeneralDocumentQpdfLibrary INTERFACE
            "${IIGENERALDOCUMENT_QPDF_INSTALL}/${CMAKE_INSTALL_LIBDIR}")
        target_link_libraries(iiGeneralDocumentQpdfLibrary INTERFACE
            "-Wl,-hidden-lqpdf")
    else()
        add_library(iiGeneralDocumentQpdfLibrary STATIC IMPORTED GLOBAL)
        set_target_properties(iiGeneralDocumentQpdfLibrary PROPERTIES
            IMPORTED_LOCATION
                "${IIGENERALDOCUMENT_QPDF_INSTALL}/${CMAKE_INSTALL_LIBDIR}/${CMAKE_STATIC_LIBRARY_PREFIX}qpdf${CMAKE_STATIC_LIBRARY_SUFFIX}"
            INTERFACE_INCLUDE_DIRECTORIES
                "${IIGENERALDOCUMENT_QPDF_INSTALL}/${CMAKE_INSTALL_INCLUDEDIR}")
        if(UNIX)
            set_property(TARGET iiGeneralDocumentQpdfLibrary APPEND PROPERTY
                INTERFACE_LINK_OPTIONS "LINKER:--exclude-libs,libqpdf.a")
        endif()
    endif()
    add_dependencies(iiGeneralDocumentQpdfLibrary iiGeneralDocumentQpdf)
    set(IIGENERALDOCUMENT_QPDF_TARGET iiGeneralDocumentQpdfLibrary)
endif()

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
    src/iiGeneralDocument.h)
add_library(iiGeneralDocument::iiGeneralDocument ALIAS iiGeneralDocument)

generate_export_header(iiGeneralDocument
    EXPORT_FILE_NAME "${CMAKE_CURRENT_BINARY_DIR}/generated/iiGeneralDocument/Export.h")

target_compile_features(iiGeneralDocument PUBLIC cxx_std_20)
target_include_directories(iiGeneralDocument PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/iiGeneralDocument>)
target_link_libraries(iiGeneralDocument
    PUBLIC
        iiXml::iiXml
        iiHtmlBlock::iiHtmlBlock
        Qt6::Core
    PRIVATE
        ${IIGENERALDOCUMENT_QPDF_TARGET}
        ${IIGENERALDOCUMENT_LIBZIP_TARGET}
        ZLIB::ZLIB
        ${IIGENERALDOCUMENT_JPEG_TARGET})
if(MSVC)
    target_compile_options(iiGeneralDocument PRIVATE /W4)
else()
    target_compile_options(iiGeneralDocument PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()
set_target_properties(iiGeneralDocument PROPERTIES
    AUTOMOC ON
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
    WINDOWS_EXPORT_ALL_SYMBOLS YES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR})

install(TARGETS iiGeneralDocument
    EXPORT iiGeneralDocumentTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY src/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/iiGeneralDocument
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
    PATTERN "*/Private/*" EXCLUDE)
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
install(FILES README.md THIRD_PARTY_NOTICES.md docs/API.md docs/ARCHITECTURE.md docs/PDF_SUPPORT.md
    docs/HTML_BLOCK_CRUD.md docs/ODF_SUPPORT.md docs/WORD_SUPPORT.md
    docs/XML_TREE_CRUD.md
    DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/iiGeneralDocument)
if(NOT IIGENERALDOCUMENT_USE_SYSTEM_QPDF)
    install(FILES
        "${IIGENERALDOCUMENT_QPDF_PREFIX}/src/iiGeneralDocumentQpdf/LICENSE.txt"
        "${IIGENERALDOCUMENT_QPDF_PREFIX}/src/iiGeneralDocumentQpdf/NOTICE.md"
        "${IIGENERALDOCUMENT_QPDF_PREFIX}/src/iiGeneralDocumentQpdf/Artistic-2.0"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/iiGeneralDocument/qpdf)
endif()
if(NOT IIGENERALDOCUMENT_USE_SYSTEM_LIBZIP)
    install(FILES
        "${IIGENERALDOCUMENT_LIBZIP_PREFIX}/src/iiGeneralDocumentLibzip/LICENSE"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/iiGeneralDocument/libzip)
endif()

if(BUILD_TESTING)
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
        ZLIB::ZLIB)
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
            tests/OdfLibreOfficeInteropTest.cpp)
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

    add_test(NAME iiGeneralDocument.InstallConsumer
        COMMAND ${CMAKE_COMMAND}
            -DIIGENERALDOCUMENT_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DIIGENERALDOCUMENT_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -DIIGENERALDOCUMENT_CMAKE_COMMAND=${CMAKE_COMMAND}
            "-DIIGENERALDOCUMENT_IIXML_DIR=${iiXml_DIR}"
            "-DIIGENERALDOCUMENT_IIHTMLBLOCK_DIR=${iiHtmlBlock_DIR}"
            "-DIIGENERALDOCUMENT_QT6_DIR=${Qt6_DIR}"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/InstallConsumerTest.cmake)

    if(UNIX)
        add_test(NAME iiGeneralDocument.SymbolIsolation
            COMMAND ${CMAKE_COMMAND}
                -DIIGENERALDOCUMENT_LIBRARY=$<TARGET_FILE:iiGeneralDocument>
                -DIIGENERALDOCUMENT_NM=${CMAKE_NM}
                -DIIGENERALDOCUMENT_APPLE=${APPLE}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/SymbolIsolationTest.cmake)
    endif()
endif()
