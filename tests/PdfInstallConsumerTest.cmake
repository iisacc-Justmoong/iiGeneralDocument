set(test_root "${IIGENERALDOCUMENT_BINARY_DIR}/pdf-install-consumer")
set(stage "${test_root}/stage")
file(REMOVE_RECURSE "${test_root}")
execute_process(COMMAND "${CMAKE_COMMAND}" --install "${IIGENERALDOCUMENT_BINARY_DIR}" --config "${IIGENERALDOCUMENT_CONFIG}" --prefix "${stage}"
    RESULT_VARIABLE _result OUTPUT_VARIABLE _output ERROR_VARIABLE _error)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "PDF package staging failed: ${_output}\n${_error}")
endif()
set(_environment "${CMAKE_COMMAND}" -E env "CPATH=${IIGENERALDOCUMENT_SOURCE_DIR}/tests/foreign-headers" --unset=CPLUS_INCLUDE_PATH
    --unset=C_INCLUDE_PATH --unset=DYLD_LIBRARY_PATH --unset=DYLD_FALLBACK_LIBRARY_PATH)
foreach(_case IN ITEMS staged relocated)
    if(_case STREQUAL "staged")
        set(_prefix "${stage}")
    else()
        file(RENAME "${stage}" "${test_root}/relocated")
        set(_prefix "${test_root}/relocated")
    endif()
    execute_process(COMMAND ${_environment} "LIBRARY_PATH=${_prefix}/lib" "${CMAKE_COMMAND}"
        -S "${IIGENERALDOCUMENT_SOURCE_DIR}/tests/pdf-consumer" -B "${test_root}/${_case}-build"
        -G Ninja "-DCMAKE_PREFIX_PATH=${_prefix}" "-DCMAKE_BUILD_TYPE=${IIGENERALDOCUMENT_CONFIG}"
        RESULT_VARIABLE _result OUTPUT_VARIABLE _output ERROR_VARIABLE _error)
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "${_case} PDF consumer configuration failed: ${_output}\n${_error}")
    endif()
    execute_process(COMMAND ${_environment} "${CMAKE_COMMAND}" --build "${test_root}/${_case}-build" --config "${IIGENERALDOCUMENT_CONFIG}"
        RESULT_VARIABLE _result OUTPUT_VARIABLE _output ERROR_VARIABLE _error)
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "${_case} PDF consumer compilation failed: ${_output}\n${_error}")
    endif()
    if(WIN32)
        set(ENV{PATH} "${_prefix}/bin;$ENV{PATH}")
    endif()
    execute_process(COMMAND ${_environment} "${test_root}/${_case}-build/iiGeneralDocumentPdfConsumer"
        RESULT_VARIABLE _result OUTPUT_VARIABLE _output ERROR_VARIABLE _error)
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "${_case} PDF consumer execution failed: ${_output}\n${_error}")
    endif()
endforeach()
