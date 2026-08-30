set(install_prefix "${IIGENERALDOCUMENT_BINARY_DIR}/install-consumer-prefix")
set(consumer_build "${IIGENERALDOCUMENT_BINARY_DIR}/install-consumer-build")
file(REMOVE_RECURSE "${install_prefix}" "${consumer_build}")

foreach(required_variable IN ITEMS
        IIGENERALDOCUMENT_IIXML_DIR
        IIGENERALDOCUMENT_IIHTMLBLOCK_DIR
        IIGENERALDOCUMENT_QT6_DIR)
    if(NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
        message(FATAL_ERROR "Required dependency package directory is missing: ${required_variable}")
    endif()
endforeach()

execute_process(
    COMMAND "${IIGENERALDOCUMENT_CMAKE_COMMAND}" --install "${IIGENERALDOCUMENT_BINARY_DIR}"
            --prefix "${install_prefix}"
    RESULT_VARIABLE install_result
    OUTPUT_VARIABLE install_output
    ERROR_VARIABLE install_error)
if(NOT install_result EQUAL 0)
    message(FATAL_ERROR "Install failed:\n${install_output}\n${install_error}")
endif()

execute_process(
    COMMAND "${IIGENERALDOCUMENT_CMAKE_COMMAND}"
            -S "${IIGENERALDOCUMENT_SOURCE_DIR}/tests/consumer"
            -B "${consumer_build}"
            -G Ninja
            "-DCMAKE_PREFIX_PATH=${install_prefix}"
            "-DiiXml_DIR=${IIGENERALDOCUMENT_IIXML_DIR}"
            "-DiiHtmlBlock_DIR=${IIGENERALDOCUMENT_IIHTMLBLOCK_DIR}"
            "-DQt6_DIR=${IIGENERALDOCUMENT_QT6_DIR}"
    RESULT_VARIABLE configure_result
    OUTPUT_VARIABLE configure_output
    ERROR_VARIABLE configure_error)
if(NOT configure_result EQUAL 0)
    message(FATAL_ERROR "Consumer configure failed:\n${configure_output}\n${configure_error}")
endif()

execute_process(
    COMMAND "${IIGENERALDOCUMENT_CMAKE_COMMAND}" --build "${consumer_build}"
    RESULT_VARIABLE build_result
    OUTPUT_VARIABLE build_output
    ERROR_VARIABLE build_error)
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "Consumer build failed:\n${build_output}\n${build_error}")
endif()

execute_process(
    COMMAND "${consumer_build}/iiGeneralDocumentConsumer"
    RESULT_VARIABLE run_result
    OUTPUT_VARIABLE run_output
    ERROR_VARIABLE run_error)
if(NOT run_result EQUAL 0)
    message(FATAL_ERROR "Consumer run failed:\n${run_output}\n${run_error}")
endif()
