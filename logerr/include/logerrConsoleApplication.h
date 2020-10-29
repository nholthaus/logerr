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
/// @file	logerrConsoleApplication.h
/// @brief	Macro Definitions for use in C++ console (command line) applications
//
//--------------------------------------------------------------------------------------------------

#ifndef LOGERR_LOGERRCONSOLEAPPLICATION_H
#define LOGERR_LOGERRCONSOLEAPPLICATION_H

//-------------------------
//	INCLUDES
//-------------------------

#include <filesystem>
#include <iostream>
#include <string_view>
#include <thread>

#include <LogFileWriter.h>
#include <LogStream.h>
#include <StackTraceException.h>
#include <StackTraceSIGSEGV.h>
#include <appinfo.h>
#include <logerr>
#include <timestampLite.h>

//----------------------------
//  MACROS
//----------------------------

#define LOGERR_CONSOLE_APP(argc, argv) \
	APPINFO::setName(std::filesystem::path(argv[0]));

/// Place at the very beginning of the `main` function.
#ifndef LOGERR_CONSOLE_APP_BEGIN
#define LOGERR_CONSOLE_APP_BEGIN                                                                                     \
	std::signal(SIGSEGV, stackTraceSIGSEGV);                                                                         \
                                                                                                                     \
	int code       = 0;                                                                                              \
	g_mainThreadID = std::this_thread::get_id();                                                                     \
                                                                                                                     \
	LogFileWriter logFileWriter;                                                                                     \
	LogStream     logStream(std::cout);                                                                              \
                                                                                                                     \
	logStream.registerLogFunction("logFileWriter", [&logFileWriter](std::string str) { logFileWriter.write(str); }); \
                                                                                                                     \
	LOGINFO << APPINFO::name() << ' ' << APPINFO::version() << " Started." << std::endl;                             \
                                                                                                                     \
	try                                                                                                              \
	{
#endif

/// Place at the very end of the `main` function.
#ifndef LOGERR_CONSOLE_APP_END
#define LOGERR_CONSOLE_APP_END                                                        \
	/* rethrow exceptions from threads*/                                              \
	std::exception_ptr exceptionPtr = g_exceptionPtr;                                 \
	g_exceptionPtr                  = nullptr;                                        \
                                                                                      \
	if (exceptionPtr)                                                                 \
	{                                                                                 \
		std::rethrow_exception(exceptionPtr);                                         \
	}                                                                                 \
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
		code = 3;                                                                     \
	}                                                                                 \
	catch (...)                                                                       \
	{                                                                                 \
		LOGERR << "ERROR: An unknown fatal error occurred. " << std::endl;            \
		LOGINFO << APPINFO::name() << " exiting due to fatal error..." << std::endl;  \
		code = 4;                                                                     \
	}                                                                                 \
                                                                                      \
	if (code == 0) LOGINFO << APPINFO::name() << " Exited Successfully" << std::endl; \
                                                                                      \
	std::exit(code);
#endif

#endif    //LOGERR_LOGERRCONSOLEAPPLICATION_H
