#include <logerrConsoleApplication.h>

using namespace std::chrono_literals;

int main(int argc, const char* argv[])
{
	LOGERR_CONSOLE_APP(argc, argv);

	LOGERR_CONSOLE_APP_BEGIN

	std::thread t = logerr::thread([]
	                               { ERR("Goodbye, cruel world!"); });

	std::this_thread::sleep_for(2s);

	if (t.joinable())
		t.join();

	LOGERR_CONSOLE_APP_END
}
