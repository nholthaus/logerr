// ---------------------------------------------------------------------------------------------------------------------
//
/// @file       QCoreAppThread
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
// ---------------------------------------------------------------------------------------------------------------------
//
/// @brief      Instantiates a QCoreApplication in a thread if none exists.
/// @details    Qt requires that a `QCoreApplication` object is instantiated before any calls to Qt APIs are made. However,
///             for libraries like `communication`, we may want to run inside a command line app that isn't necessarily
///             a Qt application from top to bottom. So the QCoreAppThread can be used to ensure that a `QCoreApplication`
///             is running inoffensively, in some other thread, if we need it to run the library but don't want to use
///             it to control the overall application state.
//
// ---------------------------------------------------------------------------------------------------------------------

#ifndef QCOREAPPTHREAD_H
#define QCOREAPPTHREAD_H

//----------------------------
//  MACROS
//----------------------------

/// Add this macro the the private section of a Qt class that may need to run in a non-qt application.
#define ENSURE_QAPP std::shared_ptr<QCoreAppThread> m_app = QCoreAppThread::instance();

//----------------------------
//  INCLUDES
//----------------------------

#include <memory>
#include <QScopedPointer>

//----------------------------
//  FORWARD DECLARATIONS
//----------------------------

class QCoreAppThreadPrivate;

//----------------------------------------------------------------------------------------------------------------------
//      CLASS: QCoreAppThread
//----------------------------------------------------------------------------------------------------------------------
/// Qt-enabled classes that may be operating in non-Qt contexts should store a copy of the shared pointer from the
/// `instance` method for the duration of their lifetime (e.g. as a private class member with default initialization)
/// to ensure that there is a running QApplication for their entire execution.
class QCoreAppThread
{
public:
	~QCoreAppThread();

	/// Return an instance to the QCoreApplication Thread. If this is a Qt app where the application is explicitely
	/// instantiated in the main function, this results in a no-op (but is safe to call). If there is no Qt application,
	/// this call will create one and manage its lifetime, or return a shared pointer to the already created instance.
	static std::shared_ptr<QCoreAppThread> instance();

private:
	// if you're trying to get an instance of this class, don't use the construct, use `instance()`,
	// or better yet, the `ENSURE_QAPP` macro.
	QCoreAppThread();

private:
	Q_DECLARE_PRIVATE(QCoreAppThread)
	QScopedPointer<QCoreAppThreadPrivate> d_ptr;
};

#endif    // QCOREAPPTHREAD_H
