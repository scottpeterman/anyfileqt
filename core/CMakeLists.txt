# anyfileqt/core/CMakeLists.txt
#
# anyfile::core -- QtCore only. No widgets, no GUI: the operation engine, the
# clipboard formats, the volume scan and the naming rules, all of which are
# tested without a display.

add_library(anyfile_core STATIC
    src/naming.cpp
    src/fileop.cpp
    src/operationqueue.cpp
    src/clipboard.cpp
    src/volumes.cpp
    src/fsinfo.cpp
    include/anyfile/naming.h
    include/anyfile/fileop.h
    include/anyfile/operationqueue.h
    include/anyfile/clipboard.h
    include/anyfile/volumes.h
    include/anyfile/fsinfo.h
)
add_library(anyfile::core ALIAS anyfile_core)

set_target_properties(anyfile_core PROPERTIES AUTOMOC ON)
target_include_directories(anyfile_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(anyfile_core PUBLIC Qt6::Core)

if(MSVC)
    target_compile_options(anyfile_core PRIVATE /W4)
    # windows.h defines min and max as macros, which breaks std::min and
    # Qt's own headers that use them.
    target_compile_definitions(anyfile_core PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
else()
    target_compile_options(anyfile_core PRIVATE -Wall -Wextra)
endif()

if(ANYFILE_BUILD_TESTS)
    add_executable(anyfile_core_tests
        tests/test_naming.cpp
        tests/test_fileops.cpp
        tests/test_clipboard.cpp
        tests/test_fsinfo.cpp
        tests/test_main.cpp
        tests/harness.h
    )
    target_link_libraries(anyfile_core_tests PRIVATE anyfile::core)
    add_test(NAME anyfile_core COMMAND anyfile_core_tests)
endif()
