#-------------------------------------------------------------------------------
#	CMAKE SETTINGS
#-------------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.17)
set(QT_MINIMUM_VERSION 5.15.0)

cmake_host_system_information(RESULT BUILD_HOSTNAME QUERY HOSTNAME)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

#-------------------------------------------------------------------------------
#	PROJECT SETTINGS
#-------------------------------------------------------------------------------

PROJECT(logerr)

#-------------------------------------------------------------------------------
#	GIT SETTINGS
#-------------------------------------------------------------------------------

# Add a custom command that produces version.cpp, plus
# a dummy output that's not actually produced, in order
# to force version.cmake to always be re-run before the build
ADD_CUSTOM_TARGET(
	gitInfo ALL
	COMMAND ${CMAKE_COMMAND} -E copy
			${CMAKE_CURRENT_SOURCE_DIR}/gitInfo.h.in
			${CMAKE_CURRENT_BINARY_DIR}/gitInfo.h.in
    COMMAND ${CMAKE_COMMAND} -P
            ${CMAKE_CURRENT_SOURCE_DIR}/cmake/gitInfo.cmake
)

#-------------------------------------------------------------------------------
#	Qt SETTINGS
#-------------------------------------------------------------------------------

find_program(QMAKE_EXECUTABLE NAMES qmake HINTS ${QTDIR} ${Qt5_DIR} ENV QTDIR Qt5_DIR PATH_SUFFIXES bin)
if(QMAKE_EXECUTABLE_FOUND)
	execute_process(
		COMMAND ${QMAKE_EXECUTABLE} -query QT_VERSION 
		OUTPUT_VARIABLE QT_VERSION 
		OUTPUT_STRIP_TRAILING_WHITESPACE
	)
else()
	set(QT_VERSION, "N/A")
endif()

#-------------------------------------------------------------------------------
#	CONFIGURATION
#-------------------------------------------------------------------------------

set(APPLICATION_NAME ${TARGET_NAME})
set(APPLICATION_VERSION ${VERSION})
set(APPLICATION_ORGANIZATION "Menari Softworks")
set(APPLICATION_ORGANIZATION_DOMAIN ${GIT_ORIGIN})

configure_file(appinfo.cpp.in appinfo.cpp @ONLY NEWLINE_STYLE UNIX)

#-------------------------------------------------------------------------------
#	SOURCE CODE
#-------------------------------------------------------------------------------

# .h files
list(APPEND HEADER_FILES
	include/appinfo.h
	include/Application.h
	include/concurrentQueue.h
	include/ExceptionDialog.h
	include/logerr
	include/logerrMacros.h
	include/LogDock.h
	include/LogFileWriter.h
	include/LogModel.h
	include/LogProxyModel.h
	include/LogStream.h
	include/StackTrace.h
	include/StackTraceException.h
	include/StackTraceSIGSEGV.h
	include/timestampLite.h
)

# .cpp files
list(APPEND SOURCE_FILES
	src/Application.cpp
	src/ExceptionDialog.cpp
	src/LogFileWriter.cpp
	src/LogDock.cpp
	src/LogModel.cpp
	src/LogProxyModel.cpp
	src/LogStream.cpp
	src/StackTrace.cpp
	src/StackTraceException.cpp
	src/StackTraceSIGSEGV.cpp
	src/timestampLite.cpp
	${CMAKE_CURRENT_BINARY_DIR}/appinfo.cpp
)

if(MSVC) 
	list(APPEND SOURCE_FILES ${HEADER_FILES})
endif()

#-------------------------------------------------------------------------------
#	ADDITIONAL LIBRARIES
#-------------------------------------------------------------------------------

list(APPEND LIBRARIES
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
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/include")

# Generate the executable
add_library(${PROJECT_NAME} ${SOURCE_FILES} ${RESOURCES})

# link libraries
target_link_libraries(${PROJECT_NAME} ${LIBRARIES})

# include directories
set_target_properties(${PROJECT_NAME} PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/include;${CMAKE_CURRENT_BINARY_DIR}")

#-------------------------------------------------------------------------------
#	ADDITIONAL DEPENDENCIES
#-------------------------------------------------------------------------------

add_dependencies(${PROJECT_NAME} gitInfo)