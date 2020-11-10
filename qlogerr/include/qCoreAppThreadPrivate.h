#ifndef LOGERR_QCOREAPPTHREADPRIVATE_H
#define LOGERR_QCOREAPPTHREADPRIVATE_H

//----------------------------
//  INCLUDES
//----------------------------

// Qt
#include <QCoreApplication>
#include <QTimer>

// std
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <thread>

// logerr
#include <logerr>

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

//----------------------------------------------------------------------------------------------------------------------
//      CLASS: QCoreAppThreadPrivate
//----------------------------------------------------------------------------------------------------------------------
struct QCoreAppThreadPrivate : public QObject
{
private:
	Q_OBJECT
public:
	QCoreAppThreadPrivate()
	{
		// Make a QCoreApplication in a thread.
		appThread = std::make_unique<std::thread>([this]
		                                          {
			                                          //   LOGINFO << "Starting QCoreAppThread..." << std::endl;

			                                          // Create the QCoreApplication in the thread
			                                          app = std::make_unique<QCoreApplication>(argc, argv);

			                                          QObject::connect(this, &QCoreAppThreadPrivate::quit, app.get(), &QCoreApplication::quit);

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
		emit quit();

		// join the application thread
		if (appThread && appThread->joinable())
			appThread->join();
	}

signals:
	void quit();

private:
	std::unique_ptr<QCoreApplication> app       = nullptr;
	std::unique_ptr<std::thread>      appThread = nullptr;
	std::atomic_bool                  appReady  = false;
	std::condition_variable           appReadyCondition;
	std::mutex                        appReadyMutex;
};

#endif    //LOGERR_QCOREAPPTHREADPRIVATE_H
