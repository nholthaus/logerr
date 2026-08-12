include_guard(GLOBAL)

if(NOT WIN32)
    return()
endif()

get_target_property(_logerr_qmake Qt6::qmake IMPORTED_LOCATION)
get_filename_component(_logerr_qt_bin_dir "${_logerr_qmake}" DIRECTORY)
find_program(LOGERR_WINDEPLOYQT_EXECUTABLE windeployqt
    HINTS "${_logerr_qt_bin_dir}"
    REQUIRED)

# Copy the Qt runtime beside a freshly built executable. This complements Qt's install-time deployment API and keeps
# CTest discovery plus direct IDE launches working on Windows.
function(logerr_windeployqt target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E env "PATH=${_logerr_qt_bin_dir}"
                "${LOGERR_WINDEPLOYQT_EXECUTABLE}"
                --verbose 0
                --no-compiler-runtime
                ${ARGN}
                "$<TARGET_FILE:${target}>"
        VERBATIM)
endfunction()
