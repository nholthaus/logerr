#include <logerrQtConsoleApplication.h>
#include <qCoreAppThread.h>

int main(int argc, const char* argv[])
{
	LOGERR_QT_CONSOLE_APP(argc, argv);

	LOGERR_QT_CONSOLE_APP_BEGIN

	ENSURE_QAPP;
	ERR("Goodbye, cruel world!");

	LOGERR_QT_CONSOLE_APP_END
}
