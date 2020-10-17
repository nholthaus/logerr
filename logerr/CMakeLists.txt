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
else ()
    list(APPEND SOURCE_FILES src/backtraceSymbols.cpp)
endif ()

#-------------------------------------------------------------------------------
#	BFD SETTINGS
#-------------------------------------------------------------------------------

if (NOT WIN32)
    find_package(Bfd)
    if (NOT Bfd_FOUND)
        message(FATAL_ERROR "Bfd not found. Install binutils using your systems package manager (on ubuntu: `sudo apt install binutils-dev libiberty-dev`)")
    endif ()
endif ()

#-------------------------------------------------------------------------------
#	CONFIGURATION
#-------------------------------------------------------------------------------

configure_file(appinfo.cpp.in appinfo.cpp @ONLY NEWLINE_STYLE UNIX)

#-------------------------------------------------------------------------------
#	ADDITIONAL LIBRARIES
#-------------------------------------------------------------------------------

if (WIN32)
    list(APPEND LIBRARIES wsock32 ws2_32)
else (WIN32)
    list(APPEND LIBRARIES bfd)
endif (WIN32)

if (BUILD_WITH_QT)
    list(APPEND LIBRARIES Qt5::Core)
endif (BUILD_WITH_QT)

list(APPEND LIBRARIES Threads::Threads)

#-------------------------------------------------------------------------------
#	GENERATE EXECUTABLE
#-------------------------------------------------------------------------------

# include dirs
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/include;${CMAKE_BINARY_DIR}")

# Generate the executable
add_library(${PROJECT_NAME} STATIC ${SOURCE_FILES})

# link libraries
target_link_libraries(${PROJECT_NAME} PUBLIC ${LIBRARIES})

# include directories
set_target_properties(${PROJECT_NAME} PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/include;${CMAKE_CURRENT_BINARY_DIR}")

# Target properties required to generate a stack trace
if (WIN32)
    set_target_properties(${PROJECT_NAME} PROPERTIES INTERFACE_COMPILE_OPTIONS "/Zi" INTERFACE_LINK_OPTIONS "/DEBUG;/OPT:REF;/OPT:ICF")
else ()
    set_target_properties(${PROJECT_NAME} PROPERTIES INTERFACE_COMPILE_OPTIONS -g)
endif ()

#-------------------------------------------------------------------------------
#	ADDITIONAL DEPENDENCIES
#-------------------------------------------------------------------------------

add_dependencies(${PROJECT_NAME} gitinfo)