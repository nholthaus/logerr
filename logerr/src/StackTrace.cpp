//------------------------
//	INCLUDES
//------------------------
#include "StackTrace.h"
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// The intent is for this to be defined (or not) by CMake. If you're not using CMake, define this
// yourself (maybe based on the _MSV_VER or __GNUC__ macros)
#ifdef WINDOWS
#define WIN32_LEAN_AND_MEAN    // Exclude rarely-used stuff from Windows headers
#include <windows.h>
#pragma warning(push)
#pragma warning(disable : 4091)
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#pragma warning(pop)
#else
#include <backtraceSymbols.h>
#include <cxxabi.h>
#include <execinfo.h>
#endif    // WINDOWS

//--------------------------------------------------------------------------------------------------
//	StackTrace ( public )
//--------------------------------------------------------------------------------------------------
StackTrace::StackTrace(unsigned int ignore /*= 0*/)
{
#ifdef WINDOWS
	unsigned int    i;
	void*           stack[MAX_FRAMES];
	unsigned short  frames;
	SYMBOL_INFO*    symbol;
	HANDLE          process;
	DWORD           dwDisplacement;
	IMAGEHLP_LINE64 line;

	process = GetCurrentProcess();

	SymInitialize(process, nullptr, TRUE);

	frames               = CaptureStackBackTrace(ignore, MAX_FRAMES, stack, nullptr);
	symbol               = (SYMBOL_INFO*) calloc(sizeof(SYMBOL_INFO) + 256 * sizeof(char), 1);
	symbol->MaxNameLen   = 255;
	symbol->SizeOfStruct = sizeof(SYMBOL_INFO);

	SymSetOptions(SYMOPT_LOAD_LINES);
	line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);

	std::vector<std::string> symbolNames;
	std::vector<ULONG64> addresses;
	std::vector<std::string> fileNames;

	for (i = 1; i < frames; ++i)
	{
		SymFromAddr(process, (DWORD64)(stack[i]), nullptr, symbol);
		if (SymGetLineFromAddr64(process, symbol->Address, &dwDisplacement, &line))
		{
			std::string filename;
			std::stringstream ss;
			ss << line.FileName;
			while (getline(ss, filename, '\\')) {};
			fileNames.emplace_back(filename).append(":").append(std::to_string(line.LineNumber));
		}
		else
		{
			fileNames.emplace_back("??:0");
		}

		addresses.emplace_back(symbol->Address);
		symbolNames.emplace_back(symbol->Name);
	}

	// get max filename length
	size_t maxFilenameLength = 0;
	for (auto& filename : fileNames)
	{
		if (filename.length() > maxFilenameLength)
			maxFilenameLength = filename.length() + 1;	}

	std::ostringstream value;

	for (int i = 0; i < frames - 1; ++i)
	{
		value << std::right << std::setw(5) << "["
		      << std::left << std::dec << std::setw(frames / 10 + 1) << (i)
		      << std::left << std::setw(4) << "]"
		      << std::left << std::setw(0) << "0x"
		      << std::right << std::hex << std::setw(16) << std::setfill('0') << addresses[i]
		      << std::left << std::setw(0) << ": "
		      << std::left << std::setw(maxFilenameLength) << std::setfill(' ') << fileNames[i]
		      << std::left << std::setw(0) << "| "
		      << std::left << symbolNames[i]
		      << std::endl;
	}

	m_value = value.str();
	free(symbol);

#else
	// storage array for stack trace address data
	void* trace[MAX_FRAMES];

	// retrieve current stack addresses
	int frames = backtrace(trace, sizeof(trace) / sizeof(void*));

	if (frames == 0)
	{
		m_value.append("<empty, possibly corrupt>\n");
		return;
	}

	// resolve addresses into strings containing
	auto&& symbols = backtraceSymbols(trace, frames);
	if (symbols.empty())
		return;

	// get max filename length
	size_t maxFilenameLength = 0;
	for (auto& [filename, functionName] : symbols)
	{
		if (filename.length() > maxFilenameLength)
			maxFilenameLength = filename.length() + 1;
	}

	// set up a string stream to write to
	std::ostringstream value{};

	// iterate over the returned symbol lines. skip the first, it is the
	// address of this function.
	for (size_t i = 1 + ignore; i < symbols.size(); i++)
	{
		auto& [filename, functionName] = symbols[i];

		if (filename.empty())
			filename = "??:0";

		// if you see "Error: invalid pointer" here you probably need to`
		// increase the size of FUNCTION_NAME_SIZE.
		int   demanglerStatus = 0;
		char* ret             = abi::__cxa_demangle(functionName.c_str(),
                                        nullptr,
                                        nullptr,
                                        &demanglerStatus);

		if (demanglerStatus == 0 && ret)
		{
			value << std::right << std::setw(5) << "["
			      << std::right << std::dec << std::setw(frames / 10 + 1) << (i - ignore)
			      << std::left << std::setw(4) << "]"
			      << std::left << std::setw(0) << "0x"
			      << std::right << std::hex << std::setw(16) << std::setfill('0') << (unsigned long long) trace[i]
			      << std::left << std::setw(0) << ": "
			      << std::left << std::setw(maxFilenameLength) << std::setfill(' ') << filename
			      << std::left << std::setw(0) << "| "
			      << std::left << ret
			      << std::endl;
		}
		else if (!functionName.empty())
		{
			// de-mangling failed, but there is a symbol name. Output function name as a C function with
			// no arguments.
			value << std::right << std::setw(5) << "["
			      << std::right << std::dec << std::setw(frames / 10 + 1) << (i - ignore)
			      << std::left << std::setw(4) << "]"
			      << std::left << std::setw(0) << "0x"
			      << std::right << std::hex << std::setw(16) << std::setfill('0') << (unsigned long long) trace[i]
			      << std::left << std::setw(0) << ": "
			      << std::left << std::setw(maxFilenameLength) << std::setfill(' ') << filename
			      << std::left << std::setw(0) << "| "
			      << std::left << functionName
			      << std::endl;
		}
		else
		{
			// No symbol name. Print the module instead
			value << std::right << std::setw(5) << "["
			      << std::right << std::dec << std::setw(frames / 10 + 1) << (i - ignore)
			      << std::left << std::setw(4) << "]"
			      << std::left << std::setw(0) << "0x"
			      << std::right << std::hex << std::setw(16) << std::setfill('0') << (unsigned long long) trace[i]
			      << std::left << std::setw(0) << ": "
			      << std::left << std::setw(maxFilenameLength) << std::setfill(' ') << filename
			      << std::left << std::setw(0) << "| "
			      << std::left << "<no symbol found>"
			      << std::endl;
		}

		if (ret) free(ret);
	}

	// store values
	m_value = value.str();
#endif
}

//--------------------------------------------------------------------------------------------------
//  data ( public )
//--------------------------------------------------------------------------------------------------
const char* StackTrace::data() const noexcept
{
	return m_value.data();
}

//--------------------------------------------------------------------------------------------------
//  operator string ( public )
//--------------------------------------------------------------------------------------------------
StackTrace::operator std::string() const
{
	return m_value;
}
