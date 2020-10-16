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

# Qt needs the header files for automoc
list(APPEND SOURCE_FILES ${HEADER_FILES})

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