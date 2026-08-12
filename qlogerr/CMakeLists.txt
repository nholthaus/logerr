set(qlogerr_headers
    include/qappinfo.h
    include/Application.h
    include/coreApplication.h
    include/ExceptionDialog.h
    include/logBlaster.h
    include/logChannel.h
    include/logDock.h
    include/logerrGuiApplication.h
    include/logerrQtConsoleApplication.h
    include/LogModel.h
    include/LogProxyModel.h
    include/logReceiver.h
    include/StackTraceSIGSEGVQt.h
    include/qCoreAppThread.h
    include/qCoreAppThreadPrivate.h
    include/QEventThread.h)

set(qlogerr_sources
    src/Application.cpp
    src/coreApplication.cpp
    src/ExceptionDialog.cpp
    src/logBlaster.cpp
    src/logDock.cpp
    src/LogModel.cpp
    src/LogProxyModel.cpp
    src/logReceiver.cpp
    src/StackTraceSIGSEGVQt.cpp
    src/qCoreAppThread.cpp)

qt_add_library(qlogerr STATIC ${qlogerr_sources} ${qlogerr_headers})
add_library(logerr::qlogerr ALIAS qlogerr)

set_target_properties(qlogerr PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON)
target_compile_features(qlogerr PUBLIC cxx_std_23)
target_include_directories(qlogerr
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
target_link_libraries(qlogerr
    PUBLIC
        logerr::logerr
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Network
    PRIVATE
        gitinfo
        Threads::Threads)

logerr_enable_project_warnings(qlogerr)
