include("${IIGENERALDOCUMENT_SOURCE_DIR}/tests/QtImportAudit.cmake")
function(expect_import text expected)
    iigeneraldocument_find_qt_import("${text}" actual)
    if(expected AND actual STREQUAL "")
        message(FATAL_ERROR "Qt import was missed: ${text}")
    elseif(NOT expected AND NOT actual STREQUAL "")
        message(FATAL_ERROR "Unrelated path was mistaken for a Qt import: ${text}")
    endif()
endfunction()
expect_import([=[/Volumes/Qt6Samples/libDocument.dylib:
    @rpath/libiiFileProviderNative.0.5.dylib (compatibility version 0.5.0, current version 0.11.0)
]=] FALSE)
expect_import([=[/project/libDocument.dylib:
    /some/QtCore.framework/Versions/A/QtCore (compatibility version 1.0.0, current version 1.0.0)
]=] TRUE)
expect_import([=[/project/libDocument.dylib:
    /some/Qt.framework/Versions/4/Qt (compatibility version 4.0.0, current version 4.8.0)
]=] TRUE)
expect_import([=[ 0x00000001 (NEEDED) Shared library: [libQt6Bluetooth.so.6]
]=] TRUE)
expect_import([=[    Qt6Core.dll
]=] TRUE)
expect_import([=[    DLL Name: QT6GUI.DLL
]=] TRUE)
expect_import([=[Dump of file C:\\Qt6Workspace\\document.dll
    KERNEL32.dll
]=] FALSE)
function(expect_symbol text expected)
    iigeneraldocument_find_qt_symbol("${text}" actual)
    if(expected AND actual STREQUAL "")
        message(FATAL_ERROR "Qt symbol was missed: ${text}")
    elseif(NOT expected AND NOT actual STREQUAL "")
        message(FATAL_ERROR "Unrelated symbol was mistaken for Qt: ${text}")
    endif()
endfunction()
expect_symbol("                 U __ZN13QNetworkReply8finishedEv" TRUE)
expect_symbol("                 U __ZNK12QApplication6activeEv" TRUE)
expect_symbol("                 U _qt_metacall" TRUE)
expect_symbol("                 U __ZN2ii3xml6markupE" FALSE)
