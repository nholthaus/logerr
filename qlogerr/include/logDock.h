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
/// @file	logDock.h
/// @brief	Dock Widget that contains the log model and view
//
//--------------------------------------------------------------------------------------------------

#pragma once
#ifndef logDock_h__
#define logDock_h__

//-------------------------
//	INCLUDES
//-------------------------

#include <concurrent_queue.h>
#include <string>

#include <QDockWidget>

//-------------------------
//	FORWARD DECLARATIONS
//-------------------------

class LogModel;
class LogProxyModel;

class QCheckBox;
class QFrame;
class QGroupBox;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QToolButton;
class QTreeView;
class QVBoxLayout;

//--------------------------------------------------------------------------------------------------
//	LogDock
//--------------------------------------------------------------------------------------------------

class LogDock : public QDockWidget
{
	Q_OBJECT

public:
	LogDock();
	~LogDock() override;

public slots:

	void queueLogEntry(std::string str);

private slots:

	void on_scrollbackBufferSize_changed();
	void on_showTimestampsCheckBox_toggled();
	void autoscroll();
	void stableScroll();
	void search(const QString& value);

private:
	LogModel*      m_logModel      = nullptr;
	LogProxyModel* m_logProxyModel = nullptr;
	QTreeView*     m_logView       = nullptr;

	QFrame*      m_topLevelWidget   = nullptr;
	QVBoxLayout* m_topLevelLayout   = nullptr;
	QHBoxLayout* m_settingsLayout   = nullptr;
	QGroupBox*   m_typesGroupbox    = nullptr;
	QGroupBox*   m_settingsGroupBox = nullptr;
	QGroupBox*   m_searchGroupBox   = nullptr;

	QCheckBox* m_errorCheckBox   = nullptr;
	QCheckBox* m_warningCheckBox = nullptr;
	QCheckBox* m_infoCheckBox    = nullptr;
	QCheckBox* m_debugCheckBox   = nullptr;

	QCheckBox* m_showTimestampsCheckBox = nullptr;

	QLabel*    m_scrollbackLabel    = nullptr;
	QLineEdit* m_scrollbackLineEdit = nullptr;
	QCheckBox* m_autoscrollCheckBox = nullptr;

	QLineEdit*   m_searchLineEdit  = nullptr;
	QToolButton* m_matchCaseButton = nullptr;
	QToolButton* m_regexButton     = nullptr;
};

#endif    // logDock_h__
