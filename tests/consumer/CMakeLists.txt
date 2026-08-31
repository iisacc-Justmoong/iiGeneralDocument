cmake_minimum_required(VERSION 3.31)
project(iiGeneralDocumentConsumer LANGUAGES CXX)

find_package(iiGeneralDocument 0.1 CONFIG REQUIRED)

get_target_property(iiGeneralDocumentDependencies
    iiGeneralDocument::iiGeneralDocument INTERFACE_LINK_LIBRARIES)
foreach(required_dependency IN ITEMS
        iiXml::iiXml
        iiHtmlBlock::iiHtmlBlock
        Qt6::Core)
    if(NOT required_dependency IN_LIST iiGeneralDocumentDependencies)
        message(FATAL_ERROR
            "iiGeneralDocument package does not expose ${required_dependency}.")
    endif()
endforeach()

add_executable(iiGeneralDocumentConsumer main.cpp)
target_link_libraries(iiGeneralDocumentConsumer PRIVATE
    iiGeneralDocument::iiGeneralDocument)
