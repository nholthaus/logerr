//----------------------------
//  INCLUDES
//---------------------------

#include "qCoreAppThread.h"
#include "qCoreAppThreadPrivate.h"

//----------------------------
//  USING NAMESPACE
//----------------------------

using namespace std::chrono_literals;

//------------------------------------
//  TRANSLATION UNIT SCOPE VARIABLES
//------------------------------------

// not a global in the `C` sense of the world because it is only visible inside this cpp file. However, logically speaking
// it's an access-controlled global.
static std::weak_ptr<QCoreAppThread> globalInstance;

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

//----------------------------------------------------------------------------------------------------------------------
//     DESTRUCTOR
//----------------------------------------------------------------------------------------------------------------------
QCoreAppThread::~QCoreAppThread()
{

}