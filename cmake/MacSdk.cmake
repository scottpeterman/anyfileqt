# anyfileqt/cmake/MacSdk.cmake
#
# Picks the macOS SDK that matches the running OS, before project() locks one in.
#
# WHY: a Command Line Tools update can install a newer SDK (MacOSX27.0) and
# point the default MacOSX.sdk symlink at it, while leaving a linker whose TAPI
# cannot read that SDK's .tbd stubs. Every link then fails with
#   ld: tapi error: malformed file ... unknown architecture arm64e.x1-macos
# The fix by hand is CMAKE_OSX_SYSROOT=<the SDK matching your macOS>; this does
# that automatically, so it keeps working when managed-Mac updates reshuffle the
# installed SDKs.
#
# Rule: among MacOSX<major>[.<minor>].sdk in the active developer directory,
# take the highest version whose major equals the running macOS's major. If
# none matches, leave CMake's default alone.
#
# Skipped entirely when the user chose: -DCMAKE_OSX_SYSROOT=... or $SDKROOT.

# Pure selection, separated so it can be tested with a fake directory:
#   cmake -DTEST_SDK_DIR=<dir> -DTEST_OS=26.7.1 -P cmake/MacSdk.cmake
function(anyfile_pick_sdk sdk_dir os_version out_var)
    set(${out_var} "" PARENT_SCOPE)
    string(REGEX MATCH "^[0-9]+" os_major "${os_version}")
    if(os_major STREQUAL "")
        return()
    endif()
    file(GLOB candidates LIST_DIRECTORIES true "${sdk_dir}/MacOSX*.sdk")
    set(best "")
    set(best_ver "")
    foreach(c IN LISTS candidates)
        get_filename_component(name "${c}" NAME)
        # MacOSX.sdk is the symlink this exists to route around.
        if(NOT name MATCHES "^MacOSX([0-9]+)(\\.([0-9]+))?\\.sdk$")
            continue()
        endif()
        set(major "${CMAKE_MATCH_1}")
        set(minor "${CMAKE_MATCH_3}")
        if(minor STREQUAL "")
            set(minor 0)
        endif()
        if(NOT major EQUAL os_major)
            continue()
        endif()
        if(best_ver STREQUAL "" OR "${major}.${minor}" VERSION_GREATER best_ver)
            set(best "${c}")
            set(best_ver "${major}.${minor}")
        endif()
    endforeach()
    set(${out_var} "${best}" PARENT_SCOPE)
endfunction()

if(CMAKE_SCRIPT_MODE_FILE AND DEFINED TEST_SDK_DIR)
    anyfile_pick_sdk("${TEST_SDK_DIR}" "${TEST_OS}" picked)
    message("picked: ${picked}")
    return()
endif()

if(DEFINED CMAKE_OSX_SYSROOT OR DEFINED ENV{SDKROOT} OR NOT EXISTS "/usr/bin/sw_vers")
    return()
endif()

execute_process(COMMAND /usr/bin/sw_vers -productVersion
                OUTPUT_VARIABLE _os OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
execute_process(COMMAND /usr/bin/xcode-select -p
                OUTPUT_VARIABLE _dev OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)

# Command Line Tools keep SDKs in <dev>/SDKs; a full Xcode keeps them inside
# the platform directory.
set(_sdk "")
foreach(_dir "${_dev}/SDKs" "${_dev}/Platforms/MacOSX.platform/Developer/SDKs")
    if(_sdk STREQUAL "" AND IS_DIRECTORY "${_dir}")
        anyfile_pick_sdk("${_dir}" "${_os}" _sdk)
    endif()
endforeach()

if(_sdk)
    set(CMAKE_OSX_SYSROOT "${_sdk}" CACHE PATH "macOS SDK, matched to the running OS by cmake/MacSdk.cmake")
    message(STATUS "macOS ${_os}: using SDK ${_sdk}")
else()
    message(STATUS "macOS ${_os}: no SDK matching major version found under ${_dev}; using the default")
endif()
