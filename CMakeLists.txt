#-------------------------------------------------------------------------------
#	CMAKE SETTINGS
#-------------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.16)
cmake_host_system_information(RESULT BUILD_HOSTNAME QUERY HOSTNAME)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

#-------------------------------------------------------------------------------
#	CMAKE OPTIONS
#-------------------------------------------------------------------------------

# Turn this on to use with Qt applications - link against the qlogerr library
# For plain C++, leave it off and link against logerr
option(BUILD_WITH_QT "Build the Qt classes" OFF)
option(BUILD_EXAMPLE "Build the example UI" OFF)

# Set this value to your companies name
set(APPLICATION_ORGANIZATION "Company Name" CACHE STRING "Set this value to your companies name")

#-------------------------------------------------------------------------------
#	PROJECT SETTINGS
#-------------------------------------------------------------------------------

project(liblogerr)

#-------------------------------------------------------------------------------
#	Qt SETTINGS
#-------------------------------------------------------------------------------

if (BUILD_WITH_QT)
    # SUPPORTED VERSIONS
    set(QT_MINIMUM_VERSION 5.15.0)
    set(QT_DEFAULT_VERSION 6.4.0)

    enable_testing(true)

    # Required Qt settings
    set(CMAKE_INCLUDE_CURRENT_DIR ON)
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTOUIC ON)
    set(CMAKE_AUTORCC ON)

    # disable warnings on the Qt sources
    if (WIN32)
        add_compile_options("/experimental:external")
        add_compile_options("/external:env:Qt5_DIR")
        add_compile_options("/external:env:Qt6_DIR")
        add_compile_options("/external:W0")
    endif (WIN32)

    # Define so the libs know whether they have Qt
    add_compile_definitions(BUILD_WITH_QT)

    # Setup the cmake prefix path so Qt can be found. Use the QTDIR variable if defined
    if (NOT DEFINED ENV{Qt6_DIR} AND NOT DEFINED ENV{Qt5_DIR})
        message(FATAL_ERROR "The environment variable `Qt6_DIR` (or `Qt5_DIR`) must be set to the install path of Qt for \
        logerr to build with Qt (e.g. \"C:/Qt/${QT_DEFAULT_VERSION}/msvc2019_64/\"). Also make sure a version of Qt >= \
        ${QT_MINIMUM_VERSION} is installed on your system.")
    endif ()

    list(APPEND CMAKE_PREFIX_PATH $ENV{Qt6_DIR} $ENV{Qt5_DIR})

    # Find the necessary Qt modules
    find_package(Qt6 COMPONENTS REQUIRED
            Core
            Gui
            Widgets
            Network
            )

    # Test for supported Qt version
    find_program(QMAKE_EXECUTABLE NAMES qmake PATH_SUFFIXES bin)
    execute_process(COMMAND ${QMAKE_EXECUTABLE} -query QT_VERSION OUTPUT_VARIABLE QT_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE)
    if (QT_VERSION VERSION_LESS QT_MINIMUM_VERSION)
        MESSAGE(FATAL_ERROR "Minimum supported Qt version: ${QT_MINIMUM_VERSION}. Installed version: ${QT_VERSION}")
    endif ()

    message(STATUS "Using Qt version: ${QT_VERSION}")
endif (BUILD_WITH_QT)

#-------------------------------------------------------------------------------
#	THREAD SETTINGS
#-------------------------------------------------------------------------------

find_package(Threads REQUIRED)

#-------------------------------------------------------------------------------
#	COMPILER SETTINGS
#-------------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)

if (MSVC)
    add_compile_options(/MP)
else(MSVC)
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif (MSVC)

#-------------------------------------------------------------------------------
#	CONFIGURATION
#-------------------------------------------------------------------------------

set(APPLICATION_NAME ${TARGET_NAME})
set(APPLICATION_VERSION ${VERSION})
set(APPLICATION_ORGANIZATION_DOMAIN ${GIT_ORIGIN})

set(IS_WIN false)
set(IS_MAC false)
set(IS_LINUX false)

if (WIN32)
    set(IS_WIN true)
elseif (APPLE)
    set(IS_MAC true)
elseif (UNIX AND NOT APPLE)
    set(IS_LINUX true)
endif ()

#-------------------------------------------------------------------------------
#	SUBDIRECTORIES
#-------------------------------------------------------------------------------

add_subdirectory(gitinfo)
add_subdirectory(logerr)

if (BUILD_WITH_QT)
    add_subdirectory(qlogerr)
    if(BUILD_EXAMPLE)
        add_subdirectory(example)
        add_subdirectory(example-console)
        add_subdirectory(example-qt-console)
    endif(BUILD_EXAMPLE)
endif (BUILD_WITH_QT)