# anyfileqt/CMakeLists.txt  (project root)
#
# anyfileqt -- a Windows-styled, dual-pane file manager for Linux, macOS and
# Windows, in C++ and Qt 6. Laid out like anyeditqt, minus the Go:
#
#   core/   anyfile::core   QtCore only: the copy/move/delete engine, the
#                           operation queue, clipboard formats, volume scan.
#                           Tested headless.
#   app/    anyfile         the Qt Widgets application.

cmake_minimum_required(VERSION 3.21)

# Before project(): the SDK is fixed when the compiler is checked. On a Mac
# whose default SDK is newer than its linker this is the difference between
# configuring and "tapi error: malformed file". No-op off macOS.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/MacSdk.cmake)

project(anyfileqt VERSION 0.1.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

option(ANYFILE_BUILD_APP   "Build the anyfile application" ON)
option(ANYFILE_BUILD_TESTS "Build the test binaries"       ON)

# At the top level, not only in the subdirectories, or ctest run against the
# top build directory finds nothing.
enable_testing()

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets)

# Which Qt, said at configure time. The bundle scripts grep these three lines:
# on a machine with a distribution Qt and an installed one, find_package takes
# whichever it reaches first, and the deploy step must use that same Qt.
get_target_property(_qtcore_loc Qt6::Core LOCATION)
get_filename_component(_qt_prefix "${Qt6_DIR}/../../.." ABSOLUTE)
message(STATUS "Qt ${Qt6_VERSION}")
message(STATUS "  prefix: ${_qt_prefix}")
message(STATUS "  QtCore: ${_qtcore_loc}")

add_subdirectory(core)

if(ANYFILE_BUILD_APP)
    add_subdirectory(app)
endif()
