#include <logerr>
#include <logerrGuiApplication.h>
#include <mainWindow.h>

int main(int argc, char** argv)
{
	LOGERR_GUI_APP_BEGIN

	MainWindow w;

	RUN_ONCE_STARTED(w.initialize(););
	RUN_ONCE_STARTED(w.show(););

	LOGERR_GUI_APP_END
}