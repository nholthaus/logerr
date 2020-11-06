// ---------------------------------------------------------------------------------------------------------------------
//
/// @file       QCoreAppThread
/// @author     Nic Holthaus
/// @date       9/28/2020
/// @copyright  (c) 2020 Systems & Technology Research. All rights reserved.
//
// ---------------------------------------------------------------------------------------------------------------------
//
/// @brief      Implementation file for QCoreAppThread
/// @details
//
// ---------------------------------------------------------------------------------------------------------------------

//----------------------------
//  INCLUDES
//---------------------------

#include "qCoreAppThread.h"

// Qt
#include <QCoreApplication>
#include <QTimer>

// std
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <thread>

//----------------------------
//  CONSTANTS
//----------------------------

static char  arg0[] = "QCoreAppThread";
static char* argv[] = {&arg0[0], NULL};
static int   argc   = (int) (sizeof(argv) / sizeof(argv[0])) - 1;

//----------------------------
//  USING NAMESPACE
//----------------------------

using namespace std::chrono_literals;

//------------------------------------
//  TRANSLATION UNIT SCOPE VARIABLES
//------------------------------------

// not a global in the `C` sense of the world because it is only visible inside this cpp file. However, logically speaking
// it's an access-controlled global.
std::weak_ptr<QCoreAppThread> globalInstance;

//----------------------------------------------------------------------------------------------------------------------
//      CLASS: QCoreAppThreadPrivate
//----------------------------------------------------------------------------------------------------------------------
struct QCoreAppThreadPrivate
{
	QCoreAppThreadPrivate()
	{
		// Make a QCoreApplication in a thread.
		appThread = std::make_unique<std::thread>([this]
		                                          {
			                                          // Create the QCoreApplication in the thread
			                                          app = std::make_unique<QCoreApplication>(argc, argv);

			                                          // once the app->exec starts, set the condition variable
			                                          QTimer::singleShot(0, [&]
			                                                             {
				                                                             appReady = true;
				                                                             appReadyCondition.notify_one();
			                                                             });

			                                          // start the application. This blocks until `app->quit()` is called
			                                          // or something causes the program to terminate with an error code.
			                                          app->exec();

			                                          // delete the QCoreApplication in the same thread it was created in,
			                                          // or else it will segfault.
			                                          app.reset(nullptr);
		                                          });

		// Wait for the application to have started (exec) before returning. Simplifies program flow for users of the class.
		std::unique_lock<std::mutex> lock(appReadyMutex);
		while (!appReady) { appReadyCondition.wait_for(lock, 1ms); }
	}

	~QCoreAppThreadPrivate()
	{
		// quit the QCoreApplication
		if (app) app->quit();

		// join the application thread
		if (appThread && appThread->joinable())
			appThread->join();
	}

private:
	std::unique_ptr<QCoreApplication> app       = nullptr;
	std::unique_ptr<std::thread>      appThread = nullptr;
	std::atomic_bool                  appReady  = false;
	std::condition_variable           appReadyCondition;
	std::mutex                        appReadyMutex;
};

//----------------------------------------------------------------------------------------------------------------------
//      FUNCTION: Destructor [public]
//----------------------------------------------------------------------------------------------------------------------
/// @brief Destructor
//----------------------------------------------------------------------------------------------------------------------
QCoreAppThread::~QCoreAppThread()
{

}

//----------------------------------------------------------------------------------------------------------------------
//  function: instance()
//----------------------------------------------------------------------------------------------------------------------
std::shared_ptr<QCoreAppThread> QCoreAppThread::instance()
{
	// lock an instance of the global core thread
	std::shared_ptr<QCoreAppThread> sharedInstance = globalInstance.lock();

	// if it's null that means no previous once exists, create it _unless_ there's already a qApp in existence.
	if (!(sharedInstance || QCoreApplication::instance()))
	{
		// instantiate the global instance
		sharedInstance.reset(new QCoreAppThread());
		globalInstance = sharedInstance;
	}

	return sharedInstance;
}

//----------------------------------------------------------------------------------------------------------------------
//      PRIVATE DEFAULT CONSTRUCTOR
//----------------------------------------------------------------------------------------------------------------------
QCoreAppThread::QCoreAppThread()
    : d_ptr(new QCoreAppThreadPrivate)
{
}
