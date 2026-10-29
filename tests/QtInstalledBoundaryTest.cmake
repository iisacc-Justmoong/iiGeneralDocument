foreach(_required IN ITEMS IIGENERALDOCUMENT_SOURCE_DIR IIGENERALDOCUMENT_BINARY_DIR
        IIGENERALDOCUMENT_NATIVE_PROVIDER_DIR IIGENERALDOCUMENT_XML_MARKUP_DIR)
    if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "Missing installed boundary test input: ${_required}")
    endif()
endforeach()
string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef _nonce)
set(_root "${IIGENERALDOCUMENT_BINARY_DIR}/qt-installed-boundary-${_nonce}")
if(EXISTS "${_root}")
    message(FATAL_ERROR "Qt installed fixture directory already exists")
endif()
file(MAKE_DIRECTORY "${_root}")
set(_stage "${_root}/prefix")
# The generated install script normally overwrites the main build's manifest.
# Relocate only that generated manifest into this unique test directory so a
# package check never changes another installation's records.
file(READ "${IIGENERALDOCUMENT_BINARY_DIR}/cmake_install.cmake" _install_script)
set(_manifest_location "${IIGENERALDOCUMENT_BINARY_DIR}/\${CMAKE_INSTALL_MANIFEST}")
string(FIND "${_install_script}" "${_manifest_location}" _manifest_position)
if(_manifest_position EQUAL -1)
    file(REMOVE_RECURSE "${_root}")
    message(FATAL_ERROR "Cannot isolate the generated install manifest")
endif()
string(REPLACE "${_manifest_location}" "${_root}/\${CMAKE_INSTALL_MANIFEST}"
    _install_script "${_install_script}")
string(REPLACE "${IIGENERALDOCUMENT_BINARY_DIR}/install_local_manifest.txt"
    "${_root}/install_local_manifest.txt" _install_script "${_install_script}")
file(WRITE "${_root}/cmake_install.cmake" "${_install_script}")
set(_install_command "${CMAKE_COMMAND}" "-DCMAKE_INSTALL_PREFIX=${_stage}")
if(DEFINED IIGENERALDOCUMENT_CONFIG AND NOT IIGENERALDOCUMENT_CONFIG STREQUAL "")
    list(APPEND _install_command "-DCMAKE_INSTALL_CONFIG_NAME=${IIGENERALDOCUMENT_CONFIG}")
endif()
list(APPEND _install_command -P "${_root}/cmake_install.cmake")
execute_process(COMMAND ${_install_command} RESULT_VARIABLE _result OUTPUT_VARIABLE _stdout ERROR_VARIABLE _stderr)
if(NOT _result EQUAL 0)
    file(REMOVE_RECURSE "${_root}")
    message(FATAL_ERROR "Could not stage document package: ${_stdout} ${_stderr}")
endif()
foreach(_case IN ITEMS safe transitive pdf)
    execute_process(COMMAND "${CMAKE_COMMAND}"
        -S "${IIGENERALDOCUMENT_SOURCE_DIR}/tests/qt-installed-boundary"
        -B "${_root}/consumer"
        "-DCMAKE_PREFIX_PATH=${_stage}"
        "-DiiFileProviderNative_DIR=${IIGENERALDOCUMENT_NATIVE_PROVIDER_DIR}"
        "-DiiXmlMarkup_DIR=${IIGENERALDOCUMENT_XML_MARKUP_DIR}"
        "-DBOUNDARY_CASE=${_case}"
        RESULT_VARIABLE _result OUTPUT_VARIABLE _stdout ERROR_VARIABLE _stderr)
    if(_case STREQUAL "transitive")
        if(_result EQUAL 0 OR NOT "${_stdout}${_stderr}" MATCHES "Qt dependency detected")
            file(REMOVE_RECURSE "${_root}")
            message(FATAL_ERROR "Installed package accepted transitive Qt: ${_stdout} ${_stderr}")
        endif()
    elseif(NOT _result EQUAL 0)
        file(REMOVE_RECURSE "${_root}")
        message(FATAL_ERROR "Installed ${_case} package was rejected: ${_stdout} ${_stderr}")
    endif()
endforeach()
file(REMOVE_RECURSE "${_root}")
