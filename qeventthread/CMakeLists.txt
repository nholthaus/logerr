# qeventthread - the QEventThread worker primitive (a jthread + private QEventLoop worker with a startup handshake,
# stop_token teardown, coalesced input/output queues, no-exception-escapes, and connectGuarded for surfacing a worker
# signal-slot fault on the application thread). Header-only, and deliberately Qt::Core ONLY: it needs no Gui/Widgets, so
# a non-GUI consumer (a transport/socket worker, a headless agent) can link it without dragging the GUI stack in. The
# Qt-GUI half of logerr (qlogerr) links this target so a GUI consumer still gets QEventThread transitively.
add_library(qeventthread INTERFACE)
add_library(logerr::qeventthread ALIAS qeventthread)

target_include_directories(qeventthread
    INTERFACE
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")

target_compile_features(qeventthread INTERFACE cxx_std_23)

# QEventThread.h includes <logerr> + <concurrent_queue.h> (both from logerr::logerr) and Qt::Core headers
# (QCoreApplication/QEventLoop/QMetaObject/QObject/QThread/QTimer). No Gui/Widgets. Threads for std::jthread.
target_link_libraries(qeventthread
    INTERFACE
        logerr::logerr
        Qt6::Core
        Threads::Threads)
