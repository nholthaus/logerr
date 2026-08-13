cmake_minimum_required(VERSION 3.25)

project(liblogerr LANGUAGES CXX)

include(CTest)

option(BUILD_WITH_QT "Build the Qt integration library" OFF)
option(BUILD_EXAMPLE "Build the example applications" OFF)
set(APPLICATION_ORGANIZATION "Company Name" CACHE STRING "Organization embedded in application metadata")

list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
cmake_host_system_information(RESULT BUILD_HOSTNAME QUERY HOSTNAME)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Strict project warnings are deliberately PRIVATE. Consumers inherit only requirements needed to compile the public
# headers and produce usable stack traces.
function(logerr_enable_project_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /MP /permissive- /Zc:__cplusplus /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endfunction()

find_package(Threads REQUIRED)

if(BUILD_WITH_QT)
    # Standard Qt package discovery: callers may use CMAKE_PREFIX_PATH or the Qt6_DIR CMake cache variable. No
    # environment variable is required and no Qt search path is propagated to consumers.
    set(logerr_qt_components Core Gui Widgets Network)
    if(BUILD_TESTING)
        list(APPEND logerr_qt_components Test)
    endif()
    find_package(Qt6 6.4 REQUIRED COMPONENTS ${logerr_qt_components})
    qt_standard_project_setup()
    set(QT_VERSION "${Qt6_VERSION}")
    message(STATUS "Using Qt version: ${Qt6_VERSION}")
endif()

set(IS_WIN false)
set(IS_MAC false)
set(IS_LINUX false)
if(WIN32)
    set(IS_WIN true)
elseif(APPLE)
    set(IS_MAC true)
elseif(UNIX)
    set(IS_LINUX true)
endif()

add_subdirectory(gitinfo)
add_subdirectory(logerr)

if(BUILD_WITH_QT)
    add_subdirectory(qeventthread)    # Qt::Core-only worker primitive; qlogerr links it so GUI consumers get it too
    add_subdirectory(qlogerr)
endif()

if(BUILD_EXAMPLE)
    add_subdirectory(example-console)
    if(BUILD_WITH_QT)
        add_subdirectory(example)
        add_subdirectory(example-qt-console)
    endif()
endif()

if(BUILD_TESTING)
    add_subdirectory(test)
endif()
