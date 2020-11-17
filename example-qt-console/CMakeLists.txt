#-------------------------------------------------------------------------------
#	CMAKE SETTINGS
#-------------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.16)

#-------------------------------------------------------------------------------
#	PROJECT SETTINGS
#-------------------------------------------------------------------------------

PROJECT(logerrExample-qt-console)

#-------------------------------------------------------------------------------
#	SOURCE CODE
#-------------------------------------------------------------------------------

# .h files
list(APPEND HEADER_FILES

        )

# .cpp files
list(APPEND SOURCE_FILES
        main.cpp
        )

list(APPEND SOURCE_FILES ${HEADER_FILES})

#-------------------------------------------------------------------------------
#	ADDITIONAL LIBRARIES
#-------------------------------------------------------------------------------

list(APPEND LIBRARIES
        qlogerr
        )

# Remove duplicate library dependencies.
list(REMOVE_DUPLICATES LIBRARIES)

#-------------------------------------------------------------------------------
#	GENERATE TEST EXECUTABLE
#-------------------------------------------------------------------------------

# Generate the executable
add_executable(${PROJECT_NAME} ${SOURCE_FILES} ${RESOURCES})

# link libraries
target_link_libraries(${PROJECT_NAME} ${LIBRARIES})

# Use the automatically determined RPATH values
set_target_properties(${PROJECT_NAME} PROPERTIES INSTALL_RPATH_USE_LINK_PATH TRUE)