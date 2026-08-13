include(FetchContent)

find_package(GTest 1.17 CONFIG QUIET)
if(NOT GTest_FOUND)
    FetchContent_Declare(googletest
        URL
            https://codeload.github.com/google/googletest/tar.gz/refs/tags/v1.17.0
            https://github.com/google/googletest/archive/refs/tags/v1.17.0.tar.gz
        URL_HASH SHA256=65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(googletest)
endif()
include(GoogleTest)

# Fail configuration if a repository-only policy becomes part of logerr's consumer contract. C++23 and symbol options
# are intentional public requirements; warning flags, Qt search mechanics, and implementation definitions are not.
get_target_property(logerr_interface_definitions logerr INTERFACE_COMPILE_DEFINITIONS)
if(logerr_interface_definitions MATCHES "AUTO_DOWNLOAD|BUILD_WITH_QT|USE_OLD_BFD|WINDOWS|_CRT_SECURE_NO_WARNINGS")
    message(FATAL_ERROR "logerr leaks a private compile definition: ${logerr_interface_definitions}")
endif()

get_target_property(logerr_interface_options logerr INTERFACE_COMPILE_OPTIONS)
if(logerr_interface_options MATCHES "(^|;)(-Wall|-Wextra|-Wpedantic|-Werror|/W[0-4]|/WX|/MP|/external:)")
    message(FATAL_ERROR "logerr leaks repository warning/build options: ${logerr_interface_options}")
endif()

add_executable(logerrCoreTests test_logerr.cpp)
target_link_libraries(logerrCoreTests PRIVATE logerr::logerr GTest::gtest_main)
logerr_enable_project_warnings(logerrCoreTests)

if(BUILD_WITH_QT)
    get_target_property(qlogerr_interface_definitions qlogerr INTERFACE_COMPILE_DEFINITIONS)
    if(qlogerr_interface_definitions MATCHES "AUTO_DOWNLOAD|BUILD_WITH_QT|USE_OLD_BFD|WINDOWS|_CRT_SECURE_NO_WARNINGS")
        message(FATAL_ERROR "qlogerr leaks a private compile definition: ${qlogerr_interface_definitions}")
    endif()

    get_target_property(qlogerr_interface_options qlogerr INTERFACE_COMPILE_OPTIONS)
    if(qlogerr_interface_options MATCHES "(^|;)(-Wall|-Wextra|-Wpedantic|-Werror|/W[0-4]|/WX|/MP|/external:)")
        message(FATAL_ERROR "qlogerr leaks repository warning/build options: ${qlogerr_interface_options}")
    endif()

    add_executable(logerrQtTests test_qeventthread.cpp)
    add_executable(qeventthreadTests test_eventthread_main.cpp test_eventThread.h)
    add_executable(coreApplicationTests test_coreapplication.cpp)
    add_executable(qcoreappthreadTests test_qcoreappthread.cpp)

    target_link_libraries(logerrQtTests PRIVATE logerr::qlogerr Qt6::Test GTest::gtest)
    target_link_libraries(qeventthreadTests PRIVATE logerr::qeventthread Qt6::Test GTest::gtest)
    target_link_libraries(coreApplicationTests PRIVATE logerr::qlogerr GTest::gtest)
    target_link_libraries(qcoreappthreadTests PRIVATE logerr::qlogerr GTest::gtest_main)
    logerr_enable_project_warnings(logerrQtTests)
    logerr_enable_project_warnings(qeventthreadTests)
    logerr_enable_project_warnings(coreApplicationTests)
    logerr_enable_project_warnings(qcoreappthreadTests)

    if(WIN32)
        # GoogleTest discovery and direct IDE launches execute the binaries immediately, so deploy before discovery.
        include(windeployqt)
        logerr_windeployqt(logerrCoreTests)
        logerr_windeployqt(logerrQtTests --include-plugins qoffscreen)
        logerr_windeployqt(qeventthreadTests)
        logerr_windeployqt(coreApplicationTests)
        logerr_windeployqt(qcoreappthreadTests)
    endif()

    gtest_discover_tests(logerrQtTests DISCOVERY_TIMEOUT 30
        PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    gtest_discover_tests(qeventthreadTests DISCOVERY_TIMEOUT 30
        PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    gtest_discover_tests(coreApplicationTests DISCOVERY_TIMEOUT 30)
    gtest_discover_tests(qcoreappthreadTests DISCOVERY_TIMEOUT 30)
endif()

# Keep discovery after Windows deployment: POST_BUILD commands execute in registration order, and discovery launches
# the executable immediately.
gtest_discover_tests(logerrCoreTests DISCOVERY_TIMEOUT 30)
