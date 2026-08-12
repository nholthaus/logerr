list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
set(WORKING_DIR "${CMAKE_SOURCE_DIR}")
include(gitinfo)

add_custom_target(gitinfo-cfg ALL
    COMMAND "${CMAKE_COMMAND}" -E copy
            "${CMAKE_CURRENT_SOURCE_DIR}/gitinfo.h.in"
            "${CMAKE_CURRENT_BINARY_DIR}/gitinfo.h.in"
    COMMAND "${CMAKE_COMMAND}" "-DWORKING_DIR=${CMAKE_SOURCE_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/gitinfo.cmake"
    VERBATIM)

add_library(gitinfo INTERFACE)
add_library(logerr::gitinfo ALIAS gitinfo)
target_include_directories(gitinfo INTERFACE "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>")
add_dependencies(gitinfo gitinfo-cfg)
