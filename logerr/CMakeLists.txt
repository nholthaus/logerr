#-------------------------------------------------------------------------------
#	CMAKE SETTINGS
#-------------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.16)

#-------------------------------------------------------------------------------
#	PROJECT SETTINGS
#-------------------------------------------------------------------------------

PROJECT(logerr)

#-------------------------------------------------------------------------------
#	SOURCE CODE
#-------------------------------------------------------------------------------

# .h files
list(APPEND HEADER_FILES
        include/appinfo.h
        include/concurrent_queue.h
        include/logerr
        include/logerrMacros.h
        include/LogFileWriter.h
        include/logerrConsoleApplication.h
        include/LogStream.h
        include/StackTrace.h
        include/StackTraceException.h
        include/StackTraceSIGSEGV.h
        include/timestampLite.h
        )

# .cpp files
list(APPEND SOURCE_FILES
        src/backtraceSymbols.cpp
        src/LogFileWriter.cpp
        src/LogStream.cpp
        src/StackTrace.cpp
        src/StackTraceException.cpp
        src/StackTraceSIGSEGV.cpp
        src/timestampLite.cpp
        ${CMAKE_CURRENT_BINARY_DIR}/appinfo.cpp
        )

if (MSVC)
    list(APPEND SOURCE_FILES ${HEADER_FILES})
endif ()

#-------------------------------------------------------------------------------
#	BFD SETTINGS
#-------------------------------------------------------------------------------

if (NOT WIN32)
    find_package(Bfd REQUIRED)
    if (NOT LIBBFD_FOUND)
        message(FATAL_ERROR "Install binutils using your systems package manager (on ubuntu: `sudo apt install binutils-dev`)")
    endif ()
endif ()

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

configure_file(appinfo.cpp.in appinfo.cpp @ONLY NEWLINE_STYLE UNIX)

#-------------------------------------------------------------------------------
#	ADDITIONAL LIBRARIES
#-------------------------------------------------------------------------------

if (NOT WIN32)
    list(APPEND LIBRARIES bfd)
endif (NOT WIN32)

#-------------------------------------------------------------------------------
#	GENERATE EXECUTABLE
#-------------------------------------------------------------------------------

# include dirs
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/include;${CMAKE_BINARY_DIR}")

# Generate the executable
add_library(${PROJECT_NAME} STATIC ${SOURCE_FILES} ${RESOURCES})

# link libraries
target_link_libraries(${PROJECT_NAME} ${LIBRARIES})

# include directories
set_target_properties(${PROJECT_NAME} PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/include;${CMAKE_CURRENT_BINARY_DIR};${CMAKE_BINARY_DIR}")

#-------------------------------------------------------------------------------
#	ADDITIONAL DEPENDENCIES
#-------------------------------------------------------------------------------

add_dependencies(${PROJECT_NAME} gitInfo)