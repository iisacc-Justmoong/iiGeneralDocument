# SDK-owned byte/object codec. No Engineering project, Qt, or system backend is loaded.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/third_party/dependencies.json" _pdf_dependencies)
string(JSON _pdf_dependency_count LENGTH "${_pdf_dependencies}" dependencies)
math(EXPR _pdf_dependency_last "${_pdf_dependency_count} - 1")
foreach(_index RANGE 0 ${_pdf_dependency_last})
    foreach(_key IN ITEMS name archive sha256)
        string(JSON _${_key} GET "${_pdf_dependencies}" dependencies ${_index} ${_key})
    endforeach()
    file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/third_party/${_archive}" _actual_sha)
    if(NOT _actual_sha STREQUAL _sha256)
        message(FATAL_ERROR "Pinned PDF dependency archive mismatch: ${_name}")
    endif()
    string(JSON _license_count LENGTH "${_pdf_dependencies}" dependencies ${_index} licenses)
    math(EXPR _license_last "${_license_count} - 1")
    foreach(_license_index RANGE 0 ${_license_last})
        string(JSON _license GET "${_pdf_dependencies}" dependencies ${_index} licenses ${_license_index})
        set(_license_path "${CMAKE_CURRENT_SOURCE_DIR}/third_party/licenses/${_name}/${_license}")
        if(NOT EXISTS "${_license_path}")
            message(FATAL_ERROR "Missing PDF dependency license: ${_license_path}")
        endif()
        string(JSON _license_sha GET "${_pdf_dependencies}" dependencies ${_index} licenseSha256 "${_license}")
        file(SHA256 "${_license_path}" _license_actual_sha)
        if(NOT _license_actual_sha STREQUAL _license_sha)
            message(FATAL_ERROR "PDF dependency license mismatch: ${_license_path}")
        endif()
        file(SIZE "${_license_path}" _license_size)
        if(_license_size EQUAL 0)
            message(FATAL_ERROR "Empty PDF dependency license: ${_license_path}")
        endif()
    endforeach()
endforeach()
include("${CMAKE_CURRENT_LIST_DIR}/PdfObjectDependencies.cmake")

add_library(iiGeneralDocumentPdf SHARED src/Pdf/Objects.cpp src/Pdf/Objects.h)
add_library(iiGeneralDocument::Pdf ALIAS iiGeneralDocumentPdf)
set_target_properties(iiGeneralDocumentPdf PROPERTIES
    EXPORT_NAME Pdf CXX_EXTENSIONS OFF AUTOMOC OFF
    EXPORT_NO_SYSTEM TRUE
    CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES
    VERSION ${PROJECT_VERSION} SOVERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}")
generate_export_header(iiGeneralDocumentPdf
    EXPORT_MACRO_NAME IIGENERALDOCUMENT_PDF_EXPORT
    EXPORT_FILE_NAME "${CMAKE_CURRENT_BINARY_DIR}/generated-pdf/iiGeneralDocument/PdfExport.h")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/src/Pdf/Objects.h"
    "${CMAKE_CURRENT_BINARY_DIR}/generated-pdf/iiGeneralDocument/Pdf/Objects.h" COPYONLY)
target_compile_features(iiGeneralDocumentPdf PUBLIC cxx_std_23)
target_include_directories(iiGeneralDocumentPdf
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated-pdf>
           $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(iiGeneralDocumentPdf PRIVATE
    iiGeneralDocumentPdfQpdf iiGeneralDocumentPdfJpeg iiGeneralDocumentPdfZlib)
if(APPLE)
    target_link_directories(iiGeneralDocumentPdf PRIVATE "${IIGENERALDOCUMENT_PDF_DEP_PREFIX}/lib")
    foreach(_archive IN ITEMS qpdf jpeg z)
        target_link_options(iiGeneralDocumentPdf PRIVATE
            "LINKER:-hidden-l${_archive}")
    endforeach()
elseif(UNIX)
    target_link_options(iiGeneralDocumentPdf PRIVATE "LINKER:--exclude-libs,ALL")
endif()
if(MSVC)
    target_compile_options(iiGeneralDocumentPdf PRIVATE /W4)
else()
    target_compile_options(iiGeneralDocumentPdf PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(IIGENERALDOCUMENT_PDF_SANITIZERS)
    if(MSVC)
        message(FATAL_ERROR "PDF sanitizer validation currently requires Clang or GCC.")
    endif()
    target_compile_options(iiGeneralDocumentPdf PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(iiGeneralDocumentPdf PRIVATE -fsanitize=address,undefined)
endif()

install(TARGETS iiGeneralDocumentPdf EXPORT iiGeneralDocumentPdfTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES src/Pdf/Objects.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/iiGeneralDocument/Pdf)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/generated-pdf/iiGeneralDocument/PdfExport.h"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/iiGeneralDocument)
install(EXPORT iiGeneralDocumentPdfTargets FILE iiGeneralDocumentPdfTargets.cmake
    NAMESPACE iiGeneralDocument:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/iiGeneralDocument)
install(DIRECTORY third_party/licenses/ DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/iiGeneralDocument/pdf)
install(FILES third_party/dependencies.json
    DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/iiGeneralDocument/pdf)
if(IIGENERALDOCUMENT_PDF_ONLY)
    configure_package_config_file(cmake/iiGeneralDocumentConfig.cmake.in
        "${CMAKE_CURRENT_BINARY_DIR}/iiGeneralDocumentConfig.cmake"
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/iiGeneralDocument)
    write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/iiGeneralDocumentConfigVersion.cmake"
        VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/iiGeneralDocumentConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/iiGeneralDocumentConfigVersion.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/iiGeneralDocument)
    install(FILES README.md THIRD_PARTY_NOTICES.md docs/ACROBAT_ABSORPTION.md
        DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/iiGeneralDocument)
endif()
if(BUILD_TESTING)
    add_executable(iiGeneralDocumentPdfObjectsTest tests/PdfObjectsTest.cpp)
    target_link_libraries(iiGeneralDocumentPdfObjectsTest PRIVATE iiGeneralDocument::Pdf)
    add_executable(iiGeneralDocumentPdfObjectBoundariesTest tests/PdfObjectBoundariesTest.cpp)
    target_link_libraries(iiGeneralDocumentPdfObjectBoundariesTest PRIVATE iiGeneralDocument::Pdf
        iiGeneralDocumentPdfQpdf iiGeneralDocumentPdfJpeg iiGeneralDocumentPdfZlib)
    if(IIGENERALDOCUMENT_PDF_SANITIZERS)
        foreach(_test_target IN ITEMS iiGeneralDocumentPdfObjectsTest iiGeneralDocumentPdfObjectBoundariesTest)
            target_compile_options(${_test_target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(${_test_target} PRIVATE -fsanitize=address,undefined)
        endforeach()
    endif()
    add_test(NAME iiGeneralDocument.PdfObjects COMMAND iiGeneralDocumentPdfObjectsTest)
    foreach(_case IN ITEMS inspection selection refusals guards fields ownership)
        add_test(NAME iiGeneralDocument.PdfObjects.${_case}
            COMMAND iiGeneralDocumentPdfObjectBoundariesTest ${_case})
    endforeach()
    if(UNIX)
        add_test(NAME iiGeneralDocument.PdfObjects.SymbolIsolation
            COMMAND ${CMAKE_COMMAND} -DIIGENERALDOCUMENT_LIBRARY=$<TARGET_FILE:iiGeneralDocumentPdf>
                -DIIGENERALDOCUMENT_NM=${CMAKE_NM} -DIIGENERALDOCUMENT_APPLE=${APPLE}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/SymbolIsolationTest.cmake)
    endif()
    if(NOT IIGENERALDOCUMENT_PDF_SANITIZERS)
        add_test(NAME iiGeneralDocument.PdfObjects.InstallConsumer
            COMMAND ${CMAKE_COMMAND}
                "-DIIGENERALDOCUMENT_CONFIG=$<CONFIG>"
                -DIIGENERALDOCUMENT_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
                -DIIGENERALDOCUMENT_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/PdfInstallConsumerTest.cmake)
    endif()
    if(APPLE)
        add_test(NAME iiGeneralDocument.PdfObjects.RuntimeDependencies
            COMMAND ${CMAKE_COMMAND} -DIIGENERALDOCUMENT_PDF_LIBRARY=$<TARGET_FILE:iiGeneralDocumentPdf>
                -DIIGENERALDOCUMENT_NM=${CMAKE_NM}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/PdfRuntimeDependenciesTest.cmake)
    endif()
endif()
