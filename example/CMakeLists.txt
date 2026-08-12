qt_add_executable(logerrExample
    main.cpp
    mainWindow.cpp
    mainWindow.h)

target_link_libraries(logerrExample PRIVATE logerr::qlogerr)
target_include_directories(logerrExample PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
logerr_enable_project_warnings(logerrExample)
set_target_properties(logerrExample PROPERTIES INSTALL_RPATH_USE_LINK_PATH TRUE)

install(TARGETS logerrExample RUNTIME DESTINATION bin)
qt_generate_deploy_app_script(
    TARGET logerrExample
    OUTPUT_SCRIPT logerr_example_deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR)
install(SCRIPT "${logerr_example_deploy_script}")

if(WIN32)
    include(windeployqt)
    logerr_windeployqt(logerrExample)
endif()

set(CPACK_GENERATOR NSIS)
set(CPACK_PACKAGE_NAME "logerrExample")
set(CPACK_PACKAGE_VENDOR "Menari Softworks")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Example Logerr UI")
set(CPACK_PACKAGE_VERSION "1.0.0")
include(CPack)
