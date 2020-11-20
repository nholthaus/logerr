//--------------------------------------------------------------------------------------------------
//
//	PAPER PUSHER
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
/// @file	mainWindow.h
/// @brief	Main Window class
//
//--------------------------------------------------------------------------------------------------

#pragma once
#ifndef mainWindow_h__
#define mainWindow_h__

//-------------------------
//	INCLUDES
//-------------------------

#include <QMainWindow>
#include <thread>

//-------------------------
//	FORWARD DECLARATIONS
//-------------------------

//--------------------------------------------------------------------------------------------------
//	MainWindow
//--------------------------------------------------------------------------------------------------

class MainWindow : public QMainWindow
{
	Q_OBJECT
public:
	MainWindow();
	~MainWindow() override;

	void initialize();

protected:
	void runThread1();
	void runThread2();
	void runThread3();

protected:
	std::thread      m_thread1;
	std::thread      m_thread2;
	std::thread      m_thread3;
	std::atomic_bool m_joinAll = false;
};

#endif    // mainWindow_h__
