add_executable(logerrExample-console main.cpp)
target_link_libraries(logerrExample-console PRIVATE logerr::logerr)
logerr_enable_project_warnings(logerrExample-console)
set_target_properties(logerrExample-console PROPERTIES INSTALL_RPATH_USE_LINK_PATH TRUE)

if(WIN32 AND BUILD_WITH_QT)
    include(windeployqt)
    logerr_windeployqt(logerrExample-console)
endif()
