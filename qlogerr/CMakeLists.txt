#-------------------------------------------------------------------------------
#	CMAKE SETTINGS
#-------------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.16)

#-------------------------------------------------------------------------------
#	PROJECT SETTINGS
#-------------------------------------------------------------------------------

PROJECT(qlogerr)

#-------------------------------------------------------------------------------
#	SOURCE CODE
#-------------------------------------------------------------------------------

# .h files
list(APPEND HEADER_FILES
        include/qappinfo.h
        include/Application.h
        include/ExceptionDialog.h
        include/LogDock.h
        include/logerrGuiApplication.h
        include/LogModel.h
        include/LogProxyModel.h
        include/StackTraceSIGSEGVQt.h
        )

# .cpp files
list(APPEND SOURCE_FILES
        src/Application.cpp
        src/ExceptionDialog.cpp
        src/LogDock.cpp
        src/LogModel.cpp
        src/LogProxyModel.cpp
        src/StackTraceSIGSEGVQt.cpp
        ${CMAKE_CURRENT_BINARY_DIR}/qappinfo.cpp
        )

if (MSVC)
    list(APPEND SOURCE_FILES ${HEADER_FILES})
endif ()

#-------------------------------------------------------------------------------
#	Qt SETTINGS
#-------------------------------------------------------------------------------

# SUPPORTED VERSIONS
set(QT_MINIMUM_VERSION 5.9.0)
set(QT_DEFAULT_VERSION 5.15.1)

enable_testing(true)

# Required Qt settings
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# disable warnings on the Qt sources
if (WIN32)
    add_compile_options("/experimental:external")
    add_compile_options("/external:env:QTDIR")
    add_compile_options("/external:W0")
endif (WIN32)

# Setup the cmake prefix path so Qt can be found. Use the QTDIR variable if defined
if (DEFINED ENV{QTDIR})
    set(QTDIR $ENV{QTDIR} CACHE STRING "Qt install path")
else ()
    MESSAGE(STATUS "`QTDIR` environment variable not set.")
    if (WIN32)
        set(QTDIR "C:/Qt/${QT_DEFAULT_VERSION}/msvc2019_64/" CACHE STRING "Qt install path")
    else ()
        set(QTDIR "/opt/Qt/${QT_DEFAULT_VERSION}/gcc_64/" CACHE STRING "Qt install path")
    endif ()
endif ()

set(QTDIR $ENV{QTDIR} CACHE STRING "Qt install path")
list(APPEND CMAKE_PREFIX_PATH ${QTDIR})

# Find the necessary Qt5 modules
find_package(Qt5 COMPONENTS REQUIRED
        Core
        Gui
        Widgets
        )

if (WIN32)
    LIST(APPEND QT_SEARCH_PATHS ${Qt5_DIR} /usr/lib64/qt5/ /opt/Qt/${QT_MINIMUM_VERSION}/gcc_64 /opt/Qt/${QT_DEFAULT_VERSION}/gcc_64)
else ()
    LIST(APPEND QT_SEARCH_PATHS ${Qt5_DIR} "C:/Qt/${QT_MINIMUM_VERSION}/msvc2019_64" "C:/Qt/${QT_DEFAULT_VERSION}/msvc2019_64" "E:/Qt/${QT_DEFAULT_VERSION}/msvc2019_64")
endif ()

# Test for supported Qt version
find_program(QMAKE_EXECUTABLE NAMES qmake
        HINTS ${QT_SEARCH_PATHS}
        ENV QTDIR
        PATH_SUFFIXES bin)
execute_process(COMMAND ${QMAKE_EXECUTABLE} -query QT_VERSION OUTPUT_VARIABLE QT_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE)
if (QT_VERSION VERSION_LESS QT_MINIMUM_VERSION)
    MESSAGE(FATAL_ERROR "Minimum supported Qt version: ${QT_MINIMUM_VERSION}. Installed version: ${QT_VERSION}")
endif ()

message(STATUS "Using Qt version: ${QT_VERSION}")

#-------------------------------------------------------------------------------
#	THREAD SETTINGS
#-------------------------------------------------------------------------------

find_package(Threads REQUIRED)

#-------------------------------------------------------------------------------
#	COMPILER SETTINGS
#-------------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)

if (MSVC)
    add_compile_options(/MP)
elseif (CMAKE_COMPILER_IS_GNUCXX)
    add_compile_options(-std=c++17)
endif (MSVC)

#-------------------------------------------------------------------------------
#	CONFIGURATION
#-------------------------------------------------------------------------------

set(APPLICATION_NAME ${TARGET_NAME})
set(APPLICATION_VERSION ${VERSION})
set(APPLICATION_ORGANIZATION "Menari Softworks")
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

configure_file(qappinfo.cpp.in qappinfo.cpp @ONLY NEWLINE_STYLE UNIX)

#-------------------------------------------------------------------------------
#	ADDITIONAL LIBRARIES
#-------------------------------------------------------------------------------

list(APPEND LIBRARIES
        logerr
        Qt5::Core
        Qt5::Gui
        Qt5::Widgets
        Threads::Threads
        )

# Remove duplicate library dependencies.
list(REMOVE_DUPLICATES LIBRARIES)

#-------------------------------------------------------------------------------
#	GENERATE EXECUTABLE
#-------------------------------------------------------------------------------

# include dirs
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/include;${CMAKE_CURRENT_BINARY_DIR}")

# Generate the executable
add_library(${PROJECT_NAME} STATIC ${SOURCE_FILES} ${RESOURCES})

# link libraries
target_link_libraries(${PROJECT_NAME} ${LIBRARIES})

# include directories
set_target_properties(${PROJECT_NAME} PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/include;${CMAKE_CURRENT_BINARY_DIR}")

#-------------------------------------------------------------------------------
#	ADDITIONAL DEPENDENCIES
#-------------------------------------------------------------------------------

add_dependencies(${PROJECT_NAME} gitInfo)