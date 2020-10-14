#include <Application.h>
#include <logerr>
#include <ExceptionDialog.h>
#include <QDebug>

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
Application::~Application()
{

}

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
		std::exception_ptr exceptionPtr = g_exceptionPtr;
		g_exceptionPtr = nullptr;

		if (exceptionPtr)
			std::rethrow_exception(exceptionPtr);
	}
	catch (const StackTraceException& e)
	{
		if(!e.fatal())
			LOGERR << e.what() << std::endl;

		ExceptionDialog dialog(e, e.fatal());
		dialog.exec();

		if (e.fatal())
			throw e;
	}
	catch (const std::exception& e)
	{
		ExceptionDialog dialog(e, true);
		dialog.exec();

		throw e;
	}
	catch (...)
	{
		const char* error = "Unhandled exception caught in Application::notify() catch-all block.";
		LOGERR << error << std::endl;
 		ExceptionDialog dialog(error, true);
 		dialog.exec();
 
		throw;
	}

	return retVal;
}

 