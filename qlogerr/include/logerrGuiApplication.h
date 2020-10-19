//--------------------------------------------------------------------------------------------------
//
//	LOGERR
//
//--------------------------------------------------------------------------------------------------
//
// The MIT License (MIT)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software
// and associated documentation files (the "Software"), to deal in the Software without
// restriction, including without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
// BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
//--------------------------------------------------------------------------------------------------
//
// Copyright (c) 2020 Nic Holthaus
//
//--------------------------------------------------------------------------------------------------
//
// ATTRIBUTION:
//
//
//--------------------------------------------------------------------------------------------------
//
/// @file	logerrGuiApplication.h
/// @brief	Macro Definitions for use in Qt GUI applications
//
//--------------------------------------------------------------------------------------------------

#ifndef LOGERR_LOGERRGUIAPPLICATION_H
#define LOGERR_LOGERRGUIAPPLICATION_H

//-------------------------
//	INCLUDES
//-------------------------

#include <filesystem>
#include <iostream>
#include <string_view>
#include <thread>

#include <QMainWindow>
#include <QTimer>

#include <Application.h>
#include <LogDock.h>
#include <LogFileWriter.h>
#include <LogStream.h>
#include <StackTraceException.h>
#include <StackTraceSIGSEGVQt.h>
#include <appinfo.h>
#include <qappinfo.h>
#include <timestampLite.h>

//-------------------------
//	HELPER FUNCTIONS
//-------------------------

namespace logerr
{
	template<class T>
	constexpr std::string_view printable(const T& value)
	{
		if constexpr (std::is_same_v<QString, std::remove_cv_t<T>>) return value.toStdString();
		else if constexpr (std::is_same_v<std::string, std::remove_cv_t<T>>)
			return value;
	}

	static QMainWindow* getMainWindow()
	{
		foreach (QWidget* w, qApp->topLevelWidgets())
			if (QMainWindow* mainWin = qobject_cast<QMainWindow*>(w))
				return mainWin;
		return nullptr;
	}
}    // namespace logerr

//----------------------------
//  MACROS
//----------------------------

// Run once the event loop starts
#define RUN_ONCE_STARTED(expression) QTimer::singleShot(0, [&] { expression });

/// Place at the very beginning of the `main` function.
#ifndef LOGERR_GUI_APP_BEGIN
#define LOGERR_GUI_APP_BEGIN                                                                                         \
	APPINFO::setName(std::filesystem::path(argv[0]).filename().replace_extension("").string());                      \
	std::signal(SIGSEGV, stackTraceSIGSEGVQt);                                                                       \
                                                                                                                     \
	int code       = 0;                                                                                              \
	g_mainThreadID = std::this_thread::get_id();                                                                     \
                                                                                                                     \
	QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);                                                         \
                                                                                                                     \
	Application app(argc, argv);                                                                                     \
	app.setOrganizationName(QAPPINFO::organization());                                                               \
	app.setOrganizationDomain(QAPPINFO::organizationDomain());                                                       \
	app.setApplicationName(QAPPINFO::name());                                                                        \
	app.setApplicationVersion(QAPPINFO::version());                                                                  \
                                                                                                                     \
	LogFileWriter logFileWriter;                                                                                     \
	LogDock*      logDock = new LogDock;                                                                             \
	LogStream     logStream(std::cout);                                                                              \
                                                                                                                     \
	logStream.registerLogFunction("logFileWriter", [&logFileWriter](std::string str) { logFileWriter.write(str); }); \
	logStream.registerLogFunction("logDock", [&logDock](std::string str) { logDock->queueLogEntry(str); });          \
                                                                                                                     \
	LOGINFO << APPINFO::name() << ' ' << APPINFO::version() << " Started." << std::endl;                             \
                                                                                                                     \
	try                                                                                                              \
	{
#endif

/// Place at the very end of the `main` function.
#ifndef LOGERR_GUI_APP_END
#define LOGERR_GUI_APP_END                                                                           \
	auto mw = logerr::getMainWindow();                                                               \
	if (mw) mw->addDockWidget(Qt::BottomDockWidgetArea, logDock);                                    \
	app.exec();                                                                                      \
	logStream.unregisterLogFunction("logDock");                                                      \
	}                                                                                                \
	catch (StackTraceException & e)                                                                  \
	{                                                                                                \
		LOGERR << e.what() << std::endl;                                                             \
		LOGINFO << APPINFO::name() << " exiting due to fatal error..." << std::endl;  \
		code = 2;                                                                                    \
	}                                                                                                \
	catch (std::exception & e)                                                                       \
	{                                                                                                \
		LOGERR << "ERROR: Caught unhandled exception -  " << e.what() << std::endl;                  \
		LOGINFO << APPINFO::name() << " exiting due to fatal error..." << std::endl;  \
		code = 2;                                                                                    \
	}                                                                                                \
	catch (...)                                                                                      \
	{                                                                                                \
		LOGERR << "ERROR: An unknown fatal error occurred. " << std::endl;                           \
		LOGINFO << APPINFO::name() << " exiting due to fatal error..." << std::endl;  \
		code = 2;                                                                                    \
	}                                                                                                \
                                                                                                     \
	if (code == 0) LOGINFO << APPINFO::name() << " Exited Successfully" << std::endl; \
                                                                                                     \
	return code;
#endif

#endif    //LOGERR_LOGERRGUIAPPLICATION_H
