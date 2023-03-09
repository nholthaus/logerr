# Set WORKING_DIR in calling code to "${CMAKE_SOURCE_DIR}"

# Use git branch as project name if clones from git
find_package(Git)

set(GIT_BRANCH "N/A" CACHE INTERNAL "git branch")
set(GIT_COMMIT_SHORT "N/A" CACHE INTERNAL "short git commit")
set(GIT_COMMIT_LONG "N/A" CACHE INTERNAL "full git commit")
set(GIT_TAG "N/A" CACHE INTERNAL "git tag")
set(GIT_ORIGIN "N/A" CACHE INTERNAL "git origin server")
set(GIT_DIRECTORY "N/A" CACHE INTERNAL "directory of the git repository")
set(GIT_REPO "N/A" CACHE INTERNAL "git repository name")
set(GIT_USER "N/A" CACHE INTERNAL "git user")
set(GIT_EMAIL "N/A" CACHE INTERNAL "Email used by git")

message(STATUS "${GIT_EXECUTABLE}")
message(STATUS "${WORKING_DIR}")

if(GIT_FOUND)
	execute_process(
	  COMMAND ${GIT_EXECUTABLE} -C ${WORKING_DIR} rev-parse --abbrev-ref HEAD
	  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
	  OUTPUT_VARIABLE RESULT
	  OUTPUT_STRIP_TRAILING_WHITESPACE
	)
	set(GIT_BRANCH ${RESULT} CACHE INTERNAL "git branch")
	execute_process(
	  COMMAND ${GIT_EXECUTABLE} -C ${WORKING_DIR} rev-parse --short HEAD
	  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
	  OUTPUT_VARIABLE RESULT
	  OUTPUT_STRIP_TRAILING_WHITESPACE
	)
	set(GIT_COMMIT_SHORT ${RESULT} CACHE INTERNAL "git branch")
	execute_process(
	  COMMAND ${GIT_EXECUTABLE} -C ${WORKING_DIR} rev-parse HEAD
	  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
	  OUTPUT_VARIABLE RESULT
	  OUTPUT_STRIP_TRAILING_WHITESPACE
	)
	set(GIT_COMMIT_LONG ${RESULT} CACHE INTERNAL "git branch")
	execute_process(
	  COMMAND ${GIT_EXECUTABLE} -C ${WORKING_DIR} describe --abbrev=0 --tags
	  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
	  OUTPUT_VARIABLE RESULT
	  OUTPUT_STRIP_TRAILING_WHITESPACE
	)
	set(GIT_TAG ${RESULT} CACHE INTERNAL "git branch")
	execute_process(
	  COMMAND ${GIT_EXECUTABLE} -C ${WORKING_DIR} config --get remote.origin.url
	  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
	  OUTPUT_VARIABLE RESULT
	  OUTPUT_STRIP_TRAILING_WHITESPACE
	)
	set(GIT_ORIGIN ${RESULT} CACHE INTERNAL "git branch")
	execute_process(
	  COMMAND ${GIT_EXECUTABLE} -C ${WORKING_DIR} rev-parse --show-toplevel
	  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
	  OUTPUT_VARIABLE RESULT
	  OUTPUT_STRIP_TRAILING_WHITESPACE
	)
	set(GIT_DIRECTORY ${RESULT} CACHE INTERNAL "git branch")
	execute_process(
	  COMMAND ${GIT_EXECUTABLE} -C ${WORKING_DIR} rev-parse --show-toplevel
	  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
	  OUTPUT_VARIABLE RESULT
	  OUTPUT_STRIP_TRAILING_WHITESPACE
	)
	set(GIT_REPO ${RESULT} CACHE INTERNAL "git branch")
	execute_process(
	  COMMAND ${GIT_EXECUTABLE} -C ${WORKING_DIR} config --get user.name
	  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
	  OUTPUT_VARIABLE RESULT
	  OUTPUT_STRIP_TRAILING_WHITESPACE
	)
	set(GIT_USER ${RESULT} CACHE INTERNAL "git branch")
	execute_process(
	  COMMAND ${GIT_EXECUTABLE} -C ${WORKING_DIR} config --get user.email
	  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
	  OUTPUT_VARIABLE RESULT
	  OUTPUT_STRIP_TRAILING_WHITESPACE
	)
	set(GIT_EMAIL ${RESULT} CACHE INTERNAL "git branch")
	execute_process(
	  COMMAND ${GIT_EXECUTABLE} -C ${WORKING_DIR} status --porcelain
	  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
	  OUTPUT_VARIABLE RESULT
	  OUTPUT_STRIP_TRAILING_WHITESPACE
	)
	string(REGEX REPLACE "\n" "" RESULT "${RESULT}")
	set(GIT_DIRTY ${RESULT} CACHE INTERNAL "git branch has local changes")

	get_filename_component(RESULT "${GIT_REPO}" NAME)
	set(GIT_REPO ${RESULT} CACHE INTERNAL "git branch")

	set(APP_NAME ${GIT_REPO} CACHE INTERNAL "Application Name")
	set(APP_VERSION ${GIT_TAG} CACHE INTERNAL "Application Version")

	if("${GIT_DIRTY}" STREQUAL "")
		set(GIT_DIRTY_STRING "" CACHE INTERNAL "")
		set(GIT_DIRTY_FLAG "no" CACHE INTERNAL "")

	else()
		set(GIT_DIRTY_STRING " (dirty)" CACHE INTERNAL "")
		set(GIT_DIRTY_FLAG "yes" CACHE INTERNAL "")
	endif()

	message(STATUS "----------------------------------------")
	message(STATUS "REPO:          " ${GIT_REPO})
	message(STATUS "BRANCH:        " ${GIT_BRANCH})
	message(STATUS "TAG:           " ${GIT_TAG})
	message(STATUS "COMMIT:        " ${GIT_COMMIT_SHORT})
	message(STATUS "LOCAL CHANGES: " ${GIT_DIRTY_FLAG})
	message(STATUS "----------------------------------------")

endif()

configure_file(gitinfo.h.in gitinfo.h @ONLY NEWLINE_STYLE UNIX)