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
#include <string>
#include <thread>

#include <QMainWindow>
#include <QTimer>

#include <Application.h>
#include <LogFileWriter.h>
#include <LogStream.h>
#include <StackTraceException.h>
#include <StackTraceSIGSEGVQt.h>
#include <appinfo.h>
#include <logBlaster.h>
#include <logDock.h>
#include <logReceiver.h>
#include <qappinfo.h>
#include <timestampLite.h>

//-------------------------
//	HELPER FUNCTIONS
//-------------------------

namespace logerr
{
	[[maybe_unused]] static QMainWindow* getMainWindow()
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

// Run once the event loop starts. The expression is DEFERRED to the next event-loop turn, so it must
// NOT reference any local that has left scope by then. Safe only for objects with process/app lifetime
// (globals, `this`-owned members that outlive the deferral). For an expression that names STACK LOCALS,
// use RUN_ONCE_STARTED_CAPTURE and copy those locals by value.
#define RUN_ONCE_STARTED(expression) QTimer::singleShot(0, [&] { expression });

// Deferred one-shot like RUN_ONCE_STARTED, but the caller NAMES the variables to copy BY VALUE into the
// lambda, so a stack local referenced by the expression is captured before the enclosing scope exits and
// cannot dangle (the lambda runs on the next event-loop turn, after that scope is gone). This avoids the
// stack-use-after-scope of a by-reference deferral without a blanket [=] (which would copy QObjects and
// silently capture `this`). Wrap the capture list in parentheses so its commas are not read as macro-arg
// separators; qApp is the context object so the one-shot is bound to the application's lifetime.
//   e.g. RUN_ONCE_STARTED_CAPTURE((automation, port), automation->start(port););
#define LOGERR_DETAIL_STRIP_PARENS(...) __VA_ARGS__
#define RUN_ONCE_STARTED_CAPTURE(captures, expression)                                                                          \
	QTimer::singleShot(0, qApp, [LOGERR_DETAIL_STRIP_PARENS captures] { expression });

/// Place at the very beginning of the `main` function.
#ifndef LOGERR_GUI_APP_BEGIN
#define LOGERR_GUI_APP_BEGIN                                                                                                    \
	std::signal(SIGSEGV, stackTraceSIGSEGVQt);                                                                                  \
                                                                                                                                \
	int code          = 0;                                                                                                      \
	g_mainThreadID    = std::this_thread::get_id();                                                                             \
	g_mainThreadIDSet = true;                                                                                                   \
                                                                                                                                \
	Application app(argc, argv);                                                                                                \
	app.setOrganizationName(QAPPINFO::organization());                                                                          \
	app.setOrganizationDomain(QAPPINFO::organizationDomain());                                                                  \
	app.setApplicationName(QAPPINFO::name());                                                                                   \
	app.setApplicationVersion(QAPPINFO::version());                                                                             \
                                                                                                                                \
	LogFileWriter logFileWriter;                                                                                                \
	LogDock*      logDock = new LogDock;                                                                                        \
	LogReceiver   logReceiver;                                                                                                  \
	LogStream     logStream(std::cout);                                                                                         \
                                                                                                                                \
	logStream.registerLogFunction("logFileWriter", [&logFileWriter](std::string str) { logFileWriter.write(std::move(str)); }); \
	logStream.registerLogFunction("logDock", [&logDock](std::string str) { logDock->queueLogEntry(std::move(str)); });          \
                                                                                                                                \
	QObject::connect(&logReceiver, &LogReceiver::readyRead, logDock, &LogDock::queueLogEntry);                                  \
                                                                                                                                \
	LOGINFO << APPINFO::name() << ' ' << APPINFO::version() << " Started." << std::endl;                                        \
                                                                                                                                \
	try                                                                                                                         \
	{
#endif

/// Place at the very end of the `main` function.
#ifndef LOGERR_GUI_APP_END
#define LOGERR_GUI_APP_END                                                            \
	auto mw = logerr::getMainWindow();                                                \
	if (mw) mw->addDockWidget(Qt::BottomDockWidgetArea, logDock);                     \
	app.exec();                                                                       \
	logStream.unregisterLogFunction("logDock");                                       \
	}                                                                                 \
	catch (StackTraceException & e)                                                   \
	{                                                                                 \
		LOGERR << e.what() << std::endl;                                              \
		LOGINFO << APPINFO::name() << " exiting due to fatal error..." << std::endl;  \
		code = 2;                                                                     \
	}                                                                                 \
	catch (std::exception & e)                                                        \
	{                                                                                 \
		LOGERR << "ERROR: Caught unhandled exception -  " << e.what() << std::endl;   \
		LOGINFO << APPINFO::name() << " exiting due to fatal error..." << std::endl;  \
		code = 2;                                                                     \
	}                                                                                 \
	catch (...)                                                                       \
	{                                                                                 \
		LOGERR << "ERROR: An unknown fatal error occurred. " << std::endl;            \
		LOGINFO << APPINFO::name() << " exiting due to fatal error..." << std::endl;  \
		code = 2;                                                                     \
	}                                                                                 \
                                                                                      \
	if (code == 0) LOGINFO << APPINFO::name() << " Exited Successfully" << std::endl; \
                                                                                      \
	return code;
#endif

#endif    //LOGERR_LOGERRGUIAPPLICATION_H
