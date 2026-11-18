string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef nonce)
set(work "${IIGENERALDOCUMENT_BINARY_DIR}/document-consumer-${nonce}")
set(install_prefix "${work}/prefix")
set(consumer_build "${work}/build")
file(MAKE_DIRECTORY "${work}")

execute_process(
    COMMAND "${IIGENERALDOCUMENT_CMAKE_COMMAND}" --install "${IIGENERALDOCUMENT_BINARY_DIR}"
            --config "${IIGENERALDOCUMENT_CONFIG}"
            --prefix "${install_prefix}"
    RESULT_VARIABLE install_result
    OUTPUT_VARIABLE install_output
    ERROR_VARIABLE install_error)
if(NOT install_result EQUAL 0)
    file(REMOVE_RECURSE "${work}")
    message(FATAL_ERROR "Install failed:\n${install_output}\n${install_error}")
endif()

foreach(license_path "LICENSE" "first-party-markup/LICENSE")
    file(READ "${install_prefix}/share/licenses/iiGeneralDocument/${license_path}" license_text)
    if(NOT license_text MATCHES "Apache License" OR NOT license_text MATCHES "Version 2.0, January 2004")
        file(REMOVE_RECURSE "${work}")
        message(FATAL_ERROR "Installed first-party Apache-2.0 license is missing: ${license_path}")
    endif()
endforeach()
file(READ "${install_prefix}/share/licenses/iiGeneralDocument/NOTICE" notice_text)
if(NOT notice_text MATCHES "Copyright \\(c\\) 2026 iisacc")
    file(REMOVE_RECURSE "${work}")
    message(FATAL_ERROR "Installed NOTICE is missing Copyright (c) 2026 iisacc")
endif()
file(READ "${install_prefix}/lib/cmake/iiGeneralDocument/iiGeneralDocumentConfig.cmake" package_config)
if(NOT package_config MATCHES "set\\(iiGeneralDocument_SPDX_LICENSE \"Apache-2.0\"\\)")
    file(REMOVE_RECURSE "${work}")
    message(FATAL_ERROR "Installed package config does not export iiGeneralDocument_SPDX_LICENSE=Apache-2.0")
endif()

set(consumer_environment_command "${IIGENERALDOCUMENT_CMAKE_COMMAND}" -E env
    --unset=CPLUS_INCLUDE_PATH --unset=C_INCLUDE_PATH
    "CPATH=${IIGENERALDOCUMENT_SOURCE_DIR}/tests/foreign-headers")
set(consumer_configure_command ${consumer_environment_command}
    "${IIGENERALDOCUMENT_CMAKE_COMMAND}")
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    # Reproduce the SDK shell environment that caused @rpath load failures.
    # All three SDK directories are implicit to the linker, but each imported
    # package must still supply its runtime lookup path to the consumer.
    set(consumer_configure_command ${consumer_environment_command}
        "LIBRARY_PATH=${install_prefix}/lib:${IIGENERALDOCUMENT_NATIVE_PROVIDER_LIBRARY_DIR}"
        "${IIGENERALDOCUMENT_CMAKE_COMMAND}")
endif()

execute_process(
    COMMAND ${consumer_configure_command}
            -S "${IIGENERALDOCUMENT_SOURCE_DIR}/tests/consumer"
            -B "${consumer_build}"
            -G Ninja
            "-DCMAKE_BUILD_TYPE=${IIGENERALDOCUMENT_CONFIG}"
            "-DCMAKE_PREFIX_PATH=${install_prefix}"
            "-DiiFileProviderNative_DIR=${IIGENERALDOCUMENT_NATIVE_PROVIDER_DIR}"
            "-DiiXmlMarkup_DIR=${IIGENERALDOCUMENT_XML_MARKUP_DIR}"
    RESULT_VARIABLE configure_result
    OUTPUT_VARIABLE configure_output
    ERROR_VARIABLE configure_error)
if(NOT configure_result EQUAL 0)
    file(REMOVE_RECURSE "${work}")
    message(FATAL_ERROR "Consumer configure failed:\n${configure_output}\n${configure_error}")
endif()

execute_process(
    COMMAND ${consumer_environment_command}
            "${IIGENERALDOCUMENT_CMAKE_COMMAND}" --build "${consumer_build}"
            --config "${IIGENERALDOCUMENT_CONFIG}"
    RESULT_VARIABLE build_result
    OUTPUT_VARIABLE build_output
    ERROR_VARIABLE build_error)
if(NOT build_result EQUAL 0)
    file(REMOVE_RECURSE "${work}")
    message(FATAL_ERROR "Consumer build failed:\n${build_output}\n${build_error}")
endif()

if(WIN32)
    set(ENV{PATH} "${install_prefix}/bin;$ENV{PATH}")
endif()
execute_process(
    COMMAND "${IIGENERALDOCUMENT_CMAKE_COMMAND}" -E env
            --unset=DYLD_LIBRARY_PATH --unset=DYLD_FALLBACK_LIBRARY_PATH
            "${consumer_build}/iiGeneralDocumentConsumer"
    WORKING_DIRECTORY "${consumer_build}"
    RESULT_VARIABLE run_result
    OUTPUT_VARIABLE run_output
    ERROR_VARIABLE run_error)
if(NOT run_result EQUAL 0)
    file(REMOVE_RECURSE "${work}")
    message(FATAL_ERROR "Consumer run failed:\n${run_output}\n${run_error}")
endif()

file(REMOVE_RECURSE "${work}")
