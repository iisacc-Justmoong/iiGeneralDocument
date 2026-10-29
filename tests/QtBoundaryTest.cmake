if(NOT DEFINED IIGENERALDOCUMENT_SOURCE_DIR OR NOT DEFINED IIGENERALDOCUMENT_BINARY_DIR)
    message(FATAL_ERROR "Qt boundary fixture needs source and binary roots")
endif()
string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef _nonce)
set(_test_root "${IIGENERALDOCUMENT_BINARY_DIR}/qt-boundary-test-${_nonce}")
if(EXISTS "${_test_root}")
    message(FATAL_ERROR "Qt boundary fixture directory already exists")
endif()
file(MAKE_DIRECTORY "${_test_root}")
foreach(_case IN ITEMS safe safe_qt_named_path direct transitive qt_alias library_path windows_library_path include_path compile_definition link_option qt5 qt_bluetooth qt_feature_definition qt_monolithic)
    execute_process(COMMAND "${CMAKE_COMMAND}"
        -S "${IIGENERALDOCUMENT_SOURCE_DIR}/tests/qt-boundary"
        -B "${_test_root}/${_case}"
        "-DIIGENERALDOCUMENT_SOURCE_DIR=${IIGENERALDOCUMENT_SOURCE_DIR}"
        "-DBOUNDARY_CASE=${_case}"
        RESULT_VARIABLE _result OUTPUT_VARIABLE _stdout ERROR_VARIABLE _stderr)
    if(_case MATCHES "^safe")
        if(NOT _result EQUAL 0)
            file(REMOVE_RECURSE "${_test_root}")
            message(FATAL_ERROR "Qt-free target graph was rejected: ${_stdout} ${_stderr}")
        endif()
    elseif(_result EQUAL 0 OR NOT "${_stdout}${_stderr}" MATCHES "Qt dependency detected")
        file(REMOVE_RECURSE "${_test_root}")
        message(FATAL_ERROR "${_case} must be rejected for a Qt dependency: ${_stdout} ${_stderr}")
    endif()
endforeach()
file(REMOVE_RECURSE "${_test_root}")
