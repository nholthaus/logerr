//------------------------
//	INCLUDES
//------------------------
#include "StackTrace.h"
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
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
	// Both DbgHelp and the BFD-backed symbolizer maintain process-global state and are not safe to call concurrently.
	static std::mutex stackTraceMutex;
	const std::lock_guard stackTraceLock(stackTraceMutex);
#ifdef WINDOWS
	const auto process = GetCurrentProcess();
	static const bool symbolsInitialized = SymInitialize(process, nullptr, TRUE) != FALSE;
	if (!symbolsInitialized)
	{
		m_value = "<unable to initialize Windows symbols>\n";
		return;
	}

	void* stack[MAX_FRAMES]{};
	const unsigned short frames = CaptureStackBackTrace(ignore, MAX_FRAMES, stack, nullptr);
	auto* rawSymbol = static_cast<SYMBOL_INFO*>(std::calloc(sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(char), 1));
	if (rawSymbol == nullptr)
	{
		m_value = "<unable to allocate Windows symbol buffer>\n";
		return;
	}
	const std::unique_ptr<SYMBOL_INFO, decltype(&std::free)> symbol(rawSymbol, &std::free);
	symbol->MaxNameLen   = MAX_SYM_NAME;
	symbol->SizeOfStruct = sizeof(SYMBOL_INFO);

	SymSetOptions(SYMOPT_LOAD_LINES);
	std::vector<std::string> symbolNames;
	std::vector<ULONG64> addresses;
	std::vector<std::string> fileNames;
	symbolNames.reserve(frames);
	addresses.reserve(frames);
	fileNames.reserve(frames);

	for (unsigned short i = 1; i < frames; ++i)
	{
		const auto address = reinterpret_cast<DWORD64>(stack[i]);
		DWORD64 symbolDisplacement = 0;
		const bool symbolResolved = SymFromAddr(process, address, &symbolDisplacement, symbol.get()) != FALSE;
		IMAGEHLP_LINE64 line{};
		line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
		DWORD lineDisplacement = 0;
		if (SymGetLineFromAddr64(process, address, &lineDisplacement, &line) != FALSE && line.FileName != nullptr)
		{
			std::string filename(line.FileName);
			if (const auto separator = filename.find_last_of("\\/"); separator != std::string::npos)
				filename.erase(0, separator + 1);
			fileNames.emplace_back(filename).append(":").append(std::to_string(line.LineNumber));
		}
		else
		{
			fileNames.emplace_back("??:0");
		}

		addresses.emplace_back(symbolResolved ? symbol->Address : address);
		symbolNames.emplace_back(symbolResolved ? symbol->Name : "<no symbol found>");
	}

	// get max filename length
	size_t maxFilenameLength = 0;
	for (auto& filename : fileNames)
	{
		if (filename.length() > maxFilenameLength)
			maxFilenameLength = filename.length() + 1;
	}
	const auto filenameWidth = static_cast<std::streamsize>(maxFilenameLength);

	std::ostringstream value;

	for (size_t i = 0; i < addresses.size(); ++i)
	{
		value << std::right << std::setw(5) << "["
		      << std::left << std::dec << std::setw(frames / 10 + 1) << (i)
		      << std::left << std::setw(4) << "]"
		      << std::left << std::setw(0) << "0x"
		      << std::right << std::hex << std::setw(16) << std::setfill('0') << addresses[i]
		      << std::left << std::setw(0) << ": "
		      << std::left << std::setw(filenameWidth) << std::setfill(' ') << fileNames[i]
		      << std::left << std::setw(0) << "| "
		      << std::left << symbolNames[i]
		      << '\n';
	}

	m_value = value.str();
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
	const auto filenameWidth = static_cast<std::streamsize>(maxFilenameLength);

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
			      << std::left << std::setw(filenameWidth) << std::setfill(' ') << filename
			      << std::left << std::setw(0) << "| "
			      << std::left << ret
			      << '\n';
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
			      << std::left << std::setw(filenameWidth) << std::setfill(' ') << filename
			      << std::left << std::setw(0) << "| "
			      << std::left << functionName
			      << '\n';
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
			      << std::left << std::setw(filenameWidth) << std::setfill(' ') << filename
			      << std::left << std::setw(0) << "| "
			      << std::left << "<no symbol found>"
			      << '\n';
		}

		if (ret) std::free(ret);
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
