//----------------------------
//  INCLUDES
//----------------------------

#include <coreApplication.h>
#include <logerr>

//--------------------------------------------------------------------------------------------------
//	CoreApplication (public ) []
//--------------------------------------------------------------------------------------------------
CoreApplication::CoreApplication(int& argc, char* argv[])
		: QCoreApplication(argc, argv)
{

}

//--------------------------------------------------------------------------------------------------
//	~CoreApplication (public ) []
//--------------------------------------------------------------------------------------------------
CoreApplication::~CoreApplication() = default;

//--------------------------------------------------------------------------------------------------
//	notify () []
//--------------------------------------------------------------------------------------------------
bool CoreApplication::notify(QObject* object, QEvent* event)
{
	bool retVal = false;

	try
	{
		retVal = QCoreApplication::notify(object, event);

		// rethrow exceptions from threads
		const std::exception_ptr exceptionPtr = logerr::takeException();

		if (exceptionPtr)
			std::rethrow_exception(exceptionPtr);
	}
	catch (const logerr::exception& e)
	{
		if(!e.fatal())
			LOGERR << e.what() << std::endl;

		if (e.fatal())
			throw;
	}
	catch (const std::exception& e)
	{
		LOGERR << e.what() << std::endl;
		throw;
	}
	catch (...)
	{
		constexpr auto error = "Unhandled exception caught in CoreApplication::notify() catch-all block.";
		LOGERR << error << std::endl;

		throw;
	}

	return retVal;
}
