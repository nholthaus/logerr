#include <logerrConsoleApplication.h>

int main(int argc, const char* argv[])
{
	LOGERR_CONSOLE_APP(argc, argv);

	LOGERR_CONSOLE_APP_BEGIN

	ERR("Goodbye, cruel world!");

	LOGERR_CONSOLE_APP_END
}
