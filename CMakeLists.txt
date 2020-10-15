#-------------------------------------------------------------------------------
#	CMAKE SETTINGS
#-------------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.16)
cmake_host_system_information(RESULT BUILD_HOSTNAME QUERY HOSTNAME)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

#-------------------------------------------------------------------------------
#	CMAKE OPTIONS
#-------------------------------------------------------------------------------

option(BUILD_WITH_QT "Build the Qt classes" OFF)

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
#	SUBDIRECTORIES
#-------------------------------------------------------------------------------

add_subdirectory(logerr)

if (BUILD_WITH_QT)
    add_subdirectory(qlogerr)
endif (BUILD_WITH_QT)

project(logerr-dummy)