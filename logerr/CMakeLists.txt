set(logerr_headers
    include/appinfo.h
    include/asyncTraceLog.h
    include/concurrent_queue.h
    include/function_view.h
    include/logerr
    include/logerrConsoleApplication.h
    include/logerrMacros.h
    include/LogFileWriter.h
    include/LogStream.h
    include/sigtermHandler.h
    include/StackTrace.h
    include/StackTraceException.h
    include/StackTraceSIGSEGV.h
    include/timestampLite.h)

set(logerr_sources
    src/asyncTraceLog.cpp
    src/LogFileWriter.cpp
    src/LogStream.cpp
    src/sigtermHandler.cpp
    src/StackTrace.cpp
    src/StackTraceException.cpp
    src/StackTraceSIGSEGV.cpp
    src/timestampLite.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/appinfo.cpp")

if(NOT WIN32)
    find_package(Bfd REQUIRED)
    list(APPEND logerr_sources src/backtraceSymbols.cpp)

    include(CheckCXXSymbolExists)
    set(CMAKE_REQUIRED_INCLUDES "${LIBBFD_INCLUDE_DIRS}")
    check_cxx_symbol_exists(bfd_section_flags "bfd.h" LOGERR_HAS_BFD_SECTION_FLAGS)
    unset(CMAKE_REQUIRED_INCLUDES)
endif()

configure_file(appinfo.cpp.in appinfo.cpp @ONLY NEWLINE_STYLE UNIX)

if(BUILD_WITH_QT)
    configure_file(../qlogerr/qappinfo.cpp.in qappinfo.cpp @ONLY NEWLINE_STYLE UNIX)
    list(APPEND logerr_sources "${CMAKE_CURRENT_BINARY_DIR}/qappinfo.cpp")
endif()

add_library(logerr STATIC ${logerr_sources} ${logerr_headers})
add_library(logerr::logerr ALIAS logerr)

target_compile_features(logerr PUBLIC cxx_std_23)
target_include_directories(logerr
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(logerr PRIVATE gitinfo Threads::Threads)

if(WIN32)
    target_compile_definitions(logerr PRIVATE WINDOWS _CRT_SECURE_NO_WARNINGS)
    target_link_libraries(logerr PRIVATE wsock32 ws2_32)
else()
    target_link_libraries(logerr PRIVATE Bfd::Bfd)
    if(NOT LOGERR_HAS_BFD_SECTION_FLAGS)
        target_compile_definitions(logerr PRIVATE USE_OLD_BFD)
    endif()
endif()

if(BUILD_WITH_QT)
    target_compile_definitions(logerr PRIVATE BUILD_WITH_QT)
    target_include_directories(logerr PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../qlogerr/include")
    target_link_libraries(logerr PRIVATE Qt6::Core)
endif()

# Symbol generation must reach the final executable: applying it only while archiving this static library cannot make
# consumer frames symbolizable. No warning policy, Qt search setting, or repository implementation switch is public.
if(MSVC)
    target_compile_options(logerr PUBLIC /Zi)
    target_link_options(logerr PUBLIC /DEBUG)
else()
    target_compile_options(logerr PUBLIC -g)
endif()

logerr_enable_project_warnings(logerr)
