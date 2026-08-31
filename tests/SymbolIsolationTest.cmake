if(IIGENERALDOCUMENT_APPLE)
    set(nm_arguments -gU "${IIGENERALDOCUMENT_LIBRARY}")
else()
    set(nm_arguments -D --defined-only "${IIGENERALDOCUMENT_LIBRARY}")
endif()

execute_process(
    COMMAND "${IIGENERALDOCUMENT_NM}" ${nm_arguments}
    RESULT_VARIABLE nm_result
    OUTPUT_VARIABLE symbols
    ERROR_VARIABLE nm_error)
if(NOT nm_result EQUAL 0)
    message(FATAL_ERROR "Unable to inspect exported symbols: ${nm_error}")
endif()

if(symbols MATCHES "QPDF|QUtil|Pl_[A-Za-z]")
    message(FATAL_ERROR "Private QPDF symbols leaked into the public ABI")
endif()

if(symbols MATCHES "(^|\\n)_?zip_(open|close|fopen|fread|file_add)([^A-Za-z0-9_]|$)")
    message(FATAL_ERROR "Private libzip symbols leaked into the public ABI")
endif()

if(NOT symbols MATCHES "ii.*document")
    message(FATAL_ERROR "iiGeneralDocument exports were not found")
endif()
