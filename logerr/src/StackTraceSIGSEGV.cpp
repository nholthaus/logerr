//----------------------------
//  INCLUDES
//----------------------------

#include <appinfo.h>
#include <date.h>
#include <StackTrace.h>
#include <logerrMacros.h>

// std
#include <filesystem>
#include <fstream>

//--------------------------------------------------------------------------------------------------
//	stackTraceSigSev (public ) [static ]
//--------------------------------------------------------------------------------------------------
void stackTraceSIGSEGV(int)
{
	// Gather the details
#ifdef _MSC_VER
	StackTrace trace(7); // determined empirically
#else
	StackTrace trace(2); // determined empirically
#endif

	std::string time = "\n\nTIME:\n\n";
	time += "    Start Time   : " + APPINFO::applicationStartTime() + "\n";
	time += "    Crash Time   : " + std::string(TimestampLite()) + "\n";
	time += "\n";

	std::string crashDetails = APPINFO::name() + " Crashed! :'(" + time + APPINFO::systemDetails() + "STACK TRACE:\n\n" + trace.data();
	LOGERR << crashDetails << std::endl;

	// make sure the directory exists
	std::filesystem::create_directories(APPINFO::crashDumpDir());
	LOGINFO << "Writing crash dump to: " << APPINFO::crashDumpDir() << std::endl;

	auto currentDateTime = date::format("%FT%TZ", date::floor<std::chrono::milliseconds>(std::chrono::system_clock::now()));
	currentDateTime.erase(std::remove(currentDateTime.begin(), currentDateTime.end(), ':'), currentDateTime.end());

	std::string crashdumpFileName = std::string("crashdump-") + currentDateTime + ".txt";
	if (!APPINFO::name().empty())
	{
		crashdumpFileName.insert(0,'-', 1);
		crashdumpFileName.insert(0, APPINFO::name());
	}

	// write a dedicated crash dump file too for good measure
	std::ofstream crashDumpFile;
	crashDumpFile.open(APPINFO::crashDumpDir() + '/' + crashdumpFileName, std::ios::out);
	crashDumpFile << crashDetails;
	crashDumpFile.flush();
	crashDumpFile.close();

	LOGINFO << APPINFO::name() << " terminated due to a fatal error (application crash). Exiting with code 1..." << std::endl;

	std::exit(1);
}

//--------------------------------------------------------------------------------------------------
//	CrashAndBurn (public ) []
//--------------------------------------------------------------------------------------------------
void CrashAndBurn()
{
	int* crash = nullptr;
	*crash = 0xDEAD;
}

