qt_add_executable(logerrExample-qt-console main.cpp)
target_link_libraries(logerrExample-qt-console PRIVATE logerr::qlogerr)
logerr_enable_project_warnings(logerrExample-qt-console)
set_target_properties(logerrExample-qt-console PROPERTIES INSTALL_RPATH_USE_LINK_PATH TRUE)

if(WIN32)
    include(windeployqt)
    logerr_windeployqt(logerrExample-qt-console)
endif()
