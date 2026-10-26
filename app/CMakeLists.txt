# anyfileqt/app/CMakeLists.txt
set(CMAKE_AUTOMOC ON)

# Everything but main.cpp is a library so the test binary can link it and
# construct a real MainWindow. Q_OBJECT headers are listed as sources: AUTOMOC
# looks for the header beside the .cpp that includes it, and with headers
# included as "app/x.h" it would not find them otherwise.
add_library(anyfile_app STATIC
    settings.cpp        settings.h
    fsproxy.cpp         fsproxy.h
    filetypes.cpp       filetypes.h
    filepane.cpp        filepane.h
    navpane.cpp         navpane.h
    conflictdialog.cpp  conflictdialog.h
    opspanel.cpp        opspanel.h
    dropaction.cpp      dropaction.h
    mainwindow.cpp      mainwindow.h
    icons.cpp           icons.h
    propertiesdialog.cpp propertiesdialog.h
    platform/platform.cpp platform/platform.h
)
target_include_directories(anyfile_app PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>)
target_link_libraries(anyfile_app PUBLIC anyfile::core Qt6::Widgets Qt6::Gui)
# On the LIBRARY, not only the executable: the About box lives in the library,
# and a define scoped to the executable would leave it reporting a default.
target_compile_definitions(anyfile_app PUBLIC ANYFILE_VERSION="${PROJECT_VERSION}")

if(MSVC)
    target_compile_options(anyfile_app PRIVATE /W4)
    target_compile_definitions(anyfile_app PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_compile_options(anyfile_app PRIVATE -Wall -Wextra)
endif()

set(ANYFILE_ASSETS "${CMAKE_CURRENT_SOURCE_DIR}/../assets")

if(CMAKE_OSX_DEPLOYMENT_TARGET)
    set(ANYFILE_MIN_MACOS "${CMAKE_OSX_DEPLOYMENT_TARGET}")
else()
    set(ANYFILE_MIN_MACOS "11.0")
endif()

# Icons are optional until assets/ exists. Listed as sources when present, so
# a changed icon rebuilds; absent, the platform's generic icon is used.
set(ANYFILE_PLATFORM_SOURCES)
set(ANYFILE_ICNS_NAME "")
if(APPLE AND EXISTS "${ANYFILE_ASSETS}/anyfile.icns")
    list(APPEND ANYFILE_PLATFORM_SOURCES "${ANYFILE_ASSETS}/anyfile.icns")
    set_source_files_properties("${ANYFILE_ASSETS}/anyfile.icns" PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources")
    set(ANYFILE_ICNS_NAME "anyfile.icns")
elseif(WIN32 AND EXISTS "${ANYFILE_ASSETS}/anyfile.ico")
    list(APPEND ANYFILE_PLATFORM_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/anyfile.rc")
endif()

add_executable(anyfile main.cpp ${ANYFILE_PLATFORM_SOURCES})
target_link_libraries(anyfile PRIVATE anyfile_app)

set_target_properties(anyfile PROPERTIES
    # /SUBSYSTEM:WINDOWS: no console window behind the app on Windows.
    # main.cpp reattaches stdout so --check still prints.
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "AnyFile"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.anyfileqt.anyfile"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_ICON_FILE "${ANYFILE_ICNS_NAME}"
    MACOSX_BUNDLE_COPYRIGHT "Copyright (C) 2026 Scott Peterman. GPLv3."
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
    OUTPUT_NAME "anyfile"
)

if(ANYFILE_BUILD_TESTS)
    add_executable(anyfile_app_tests tests/test_app.cpp)
    target_link_libraries(anyfile_app_tests PRIVATE anyfile_app)
    target_include_directories(anyfile_app_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../core/tests)
    add_test(NAME anyfile_app COMMAND anyfile_app_tests)
    # A QApplication is constructed: the views need a real style.
    set_tests_properties(anyfile_app PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
