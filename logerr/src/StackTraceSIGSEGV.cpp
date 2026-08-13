//----------------------------
//  INCLUDES
//----------------------------

#include <appinfo.h>
#include <asyncTraceLog.h>
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
	const StackTrace trace(7); // determined empirically
#else
	const StackTrace trace(2); // determined empirically
#endif

	std::string time = "\n\nTIME:\n\n";
	time += "    Start Time   : " + APPINFO::applicationStartTime() + "\n";
	time += "    Crash Time   : " + std::string(TimestampLite()) + "\n";
	time += "\n";

	const std::string crashDetails = APPINFO::name() + " Crashed! :'(" + time + APPINFO::systemDetails() + "STACK TRACE:\n\n" + trace.data();
	LOGERR << crashDetails << std::endl;

	// make sure the directory exists
	std::filesystem::create_directories(APPINFO::crashDumpDir());
	LOGINFO << "Writing crash dump to: " << APPINFO::crashDumpDir() << std::endl;

	auto currentDateTime = date::format("%FT%TZ", date::floor<std::chrono::milliseconds>(std::chrono::system_clock::now()));
	currentDateTime.erase(std::remove(currentDateTime.begin(), currentDateTime.end(), ':'), currentDateTime.end());

	std::string crashdumpFileName = std::string("crashdump-") + currentDateTime + ".txt";
	if (!APPINFO::name().empty())
		crashdumpFileName.insert(0, APPINFO::name() + '-');

	// write a dedicated crash dump file too for good measure
	const auto crashDumpPath = std::filesystem::path(APPINFO::crashDumpDir()) / crashdumpFileName;
	std::ofstream crashDumpFile(crashDumpPath, std::ios::out);
	if (crashDumpFile.is_open())
	{
		crashDumpFile << crashDetails;
		crashDumpFile.flush();
		crashDumpFile.close();
	}
	else
	{
		LOGERR << "Failed to open crash dump for writing: " << crashDumpPath.string() << std::endl;
	}

	LOGINFO << APPINFO::name() << " terminated due to a fatal error (application crash). Exiting with code 1..." << std::endl;

	// LOGERR is asynchronous: drain and stop the trace-log worker synchronously so the crash entry (and any later
	// error line above) is written before the process dies.
	logerr::flushTracedErrors();

	std::exit(1);
}

//--------------------------------------------------------------------------------------------------
//	CrashAndBurn (public ) []
//--------------------------------------------------------------------------------------------------
void CrashAndBurn()
{
	int* crash = nullptr;
	// Intentional: this public diagnostic helper verifies the installed fatal-signal path.
	// NOLINTNEXTLINE(clang-analyzer-core.NullDereference)
	*crash = 0xDEAD;
}
