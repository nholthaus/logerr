//----------------------------
//  INCLUDES
//----------------------------

#include <Application.h>
#include <logerr>
#include <ExceptionDialog.h>

//--------------------------------------------------------------------------------------------------
//	Application (public ) []
//--------------------------------------------------------------------------------------------------
Application::Application(int& argc, char* argv[])
	: QApplication(argc, argv)
{

}

//--------------------------------------------------------------------------------------------------
//	~Application (public ) []
//--------------------------------------------------------------------------------------------------
Application::~Application() = default;

//--------------------------------------------------------------------------------------------------
//	notify () []
//--------------------------------------------------------------------------------------------------
bool Application::notify(QObject* object, QEvent* event)
{
	bool retVal = false;

	try
	{
		retVal = QApplication::notify(object, event);

		// rethrow exceptions from threads
		const std::exception_ptr exceptionPtr = logerr::takeException();

		if (exceptionPtr)
			std::rethrow_exception(exceptionPtr);
	}
	catch (const logerr::exception& e)
	{
		if(!e.fatal())
			LOGERR << e.what() << std::endl;

		ExceptionDialog dialog(e, e.fatal());
		dialog.exec();

		if (e.fatal())
			throw;
	}
	catch (const std::exception& e)
	{
		ExceptionDialog dialog(e, true);
		dialog.exec();

		throw;
	}
	catch (...)
	{
		auto error = "Unhandled exception caught in Application::notify() catch-all block.";
		LOGERR << error << std::endl;
		ExceptionDialog dialog(error, true);
		dialog.exec();

		throw;
	}

	return retVal;
}

#include <moc_Application.cpp>
