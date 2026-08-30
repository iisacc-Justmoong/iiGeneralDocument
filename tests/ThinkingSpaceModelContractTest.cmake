set(model_root "${IIGENERALDOCUMENT_SOURCE_DIR}/src/ThinkingSpace")
set(android_bridge
    "${IIGENERALDOCUMENT_SOURCE_DIR}/platform/Android/src/com/iisacc/app/thinkingspace/ThinkingSpaceAndroidStorage.java")

if(NOT EXISTS "${model_root}/DocumentModel.h")
    message(FATAL_ERROR "Thinking Space public model umbrella is missing")
endif()
if(NOT EXISTS "${android_bridge}")
    message(FATAL_ERROR "Thinking Space Android document bridge is missing")
endif()

file(GLOB_RECURSE model_files LIST_DIRECTORIES FALSE
    "${model_root}/*.h"
    "${model_root}/*.hpp"
    "${model_root}/*.cpp")
list(APPEND model_files "${android_bridge}")

set(combined_source "")
foreach(model_file IN LISTS model_files)
    file(READ "${model_file}" model_source)
    string(APPEND combined_source "\n${model_source}")
    foreach(forbidden_brand IN ITEMS WhatSon whatson WHATSON)
        string(FIND "${model_source}" "${forbidden_brand}" forbidden_index)
        if(NOT forbidden_index EQUAL -1)
            file(RELATIVE_PATH relative_file "${IIGENERALDOCUMENT_SOURCE_DIR}" "${model_file}")
            message(FATAL_ERROR
                "Obsolete brand token '${forbidden_brand}' remains in ${relative_file}")
        endif()
    endforeach()
    string(REGEX MATCH "\\.ws[a-zA-Z0-9_]+" obsolete_extension "${model_source}")
    if(obsolete_extension)
        file(RELATIVE_PATH relative_file "${IIGENERALDOCUMENT_SOURCE_DIR}" "${model_file}")
        message(FATAL_ERROR
            "Obsolete format token '${obsolete_extension}' remains in ${relative_file}")
    endif()
    foreach(forbidden_identifier IN ITEMS wshub wsnote wsnbody wsnhead wsnversion wsresource)
        string(FIND "${model_source}" "${forbidden_identifier}" forbidden_index)
        if(NOT forbidden_index EQUAL -1)
            file(RELATIVE_PATH relative_file "${IIGENERALDOCUMENT_SOURCE_DIR}" "${model_file}")
            message(FATAL_ERROR
                "Obsolete identifier '${forbidden_identifier}' remains in ${relative_file}")
        endif()
    endforeach()
endforeach()

foreach(required_contract IN ITEMS
        "namespace ThinkingSpace"
        ".tsnote"
        ".tsnbody"
        ".tsnhead"
        ".tsnversion"
        ".tsresource"
        "thinkingspace.note.version.store")
    string(FIND "${combined_source}" "${required_contract}" contract_index)
    if(contract_index EQUAL -1)
        message(FATAL_ERROR "Thinking Space model contract is missing: ${required_contract}")
    endif()
endforeach()

include("${IIGENERALDOCUMENT_SOURCE_DIR}/cmake/ThinkingSpaceDocumentModelSources.cmake")
list(LENGTH IIGENERALDOCUMENT_THINKING_SPACE_SOURCES transplanted_source_count)
if(NOT transplanted_source_count EQUAL 157)
    message(FATAL_ERROR
        "Expected 157 transplanted C++ model files, found ${transplanted_source_count}")
endif()

file(READ
    "${IIGENERALDOCUMENT_SOURCE_DIR}/docs/THINKING_SPACE_DOCUMENT_MODEL.md"
    provenance_document)
string(FIND "${provenance_document}"
    "b41eb16c3742c6f25d6b64edb8f6f8d58bdab509"
    provenance_index)
if(provenance_index EQUAL -1)
    message(FATAL_ERROR "Thinking Space model provenance snapshot is not documented")
endif()
