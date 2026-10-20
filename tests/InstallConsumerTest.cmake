set(install_prefix "${IIGENERALDOCUMENT_BINARY_DIR}/install-consumer-prefix")
set(consumer_build "${IIGENERALDOCUMENT_BINARY_DIR}/install-consumer-build")
file(REMOVE_RECURSE "${install_prefix}" "${consumer_build}")

execute_process(
    COMMAND "${IIGENERALDOCUMENT_CMAKE_COMMAND}" --install "${IIGENERALDOCUMENT_BINARY_DIR}"
            --config "${IIGENERALDOCUMENT_CONFIG}"
            --prefix "${install_prefix}"
    RESULT_VARIABLE install_result
    OUTPUT_VARIABLE install_output
    ERROR_VARIABLE install_error)
if(NOT install_result EQUAL 0)
    message(FATAL_ERROR "Install failed:\n${install_output}\n${install_error}")
endif()

foreach(license_path "LICENSE" "first-party-markup/LICENSE")
    file(READ "${install_prefix}/share/licenses/iiGeneralDocument/${license_path}" license_text)
    if(NOT license_text MATCHES "MIT License")
        message(FATAL_ERROR "Installed first-party MIT license is missing: ${license_path}")
    endif()
endforeach()

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
    RESULT_VARIABLE configure_result
    OUTPUT_VARIABLE configure_output
    ERROR_VARIABLE configure_error)
if(NOT configure_result EQUAL 0)
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
    message(FATAL_ERROR "Consumer run failed:\n${run_output}\n${run_error}")
endif()
