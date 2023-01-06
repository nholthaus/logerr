#include <logerrQtConsoleApplication.h>
#include <qCoreAppThread.h>

using namespace std::chrono_literals;

int main(int argc, const char* argv[])
{
	LOGERR_QT_CONSOLE_APP(argc, argv);

	LOGERR_QT_CONSOLE_APP_BEGIN

	ENSURE_QAPP;

	auto t = logerr::thread([]{ ERR("Goodbye, cruel world!"); });

	std::this_thread::sleep_for(2s);

	if (t.joinable())
		t.join();

	LOGERR_QT_CONSOLE_APP_END
}