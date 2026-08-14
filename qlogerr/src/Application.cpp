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
		// The exception already captured its own throw-site stack. Log the CLEAN message as the [ERROR] headline and that
		// throw-site trace as the deduped footer via the shared path - NOT `LOGERR << e.what()`, which would bury the
		// message under e.what()'s fat blob and append the useless catch-site (notify) stack. A fatal exception is
		// rethrown to be logged by the terminal handler, so it is not double-logged here.
		if (!e.fatal())
			logerr::logCaughtError(e);

		ExceptionDialog dialog(e, e.fatal());
		dialog.exec();

		if (e.fatal())
			throw;
	}
	catch (const std::exception& e)
	{
		// A plain std::exception carries no logerr throw-site capture, so LOGERR here (which captures the catch-site
		// stack) is the correct source of a trace - the invariant still holds: a message headline + a deduped trace
		// footer. Without this the log had NO record of a caught std::exception, only the dialog.
		LOGERR << e.what() << std::endl;

		ExceptionDialog dialog(e, true);
		dialog.exec();

		throw;
	}
	catch (...)
	{
		// An unknown exception likewise has no throw-site capture; LOGERR gives it the catch-site trace and honors the
		// invariant (message + deduped trace footer).
		auto error = "Unhandled exception caught in Application::notify() catch-all block.";
		LOGERR << error << std::endl;
		ExceptionDialog dialog(error, true);
		dialog.exec();

		throw;
	}

	return retVal;
}

#include <moc_Application.cpp>
