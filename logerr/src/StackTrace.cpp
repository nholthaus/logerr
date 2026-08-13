//------------------------
//	INCLUDES
//------------------------
#include "StackTrace.h"
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_set>
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

namespace
{
	// Deduplication registry for deduplicateByStack. The key is a hash of the raw return-address array captured for a
	// trace, computed BEFORE any symbolization: two occurrences of the SAME call stack fold to the same key (return
	// addresses are stable within a process run), while a different path - a distinct stack, even through the very same
	// source line - folds to a different key and therefore always traces in full. This is what lets a churny error site
	// reached repeatedly by one path pay the expensive symbol resolution once, without ever collapsing two genuinely
	// different stacks into one.
	std::mutex                        g_tracedStacksMutex;
	std::unordered_set<std::uint64_t> g_tracedStacks;

	// FNV-1a over the raw frame pointers. Cheap (a handful of nanoseconds over the already-captured array) and stable
	// within a process run, so identical stacks hash identically.
	std::uint64_t hashStack(const void* const* frames, std::size_t count) noexcept
	{
		std::uint64_t hash = 1469598103934665603ULL;
		for (std::size_t i = 0; i < count; ++i)
		{
			const auto address = reinterpret_cast<std::uintptr_t>(frames[i]);
			for (std::size_t byte = 0; byte < sizeof(address); ++byte)
			{
				hash ^= static_cast<std::uint64_t>((address >> (byte * 8)) & 0xFF);
				hash *= 1099511628211ULL;
			}
		}
		return hash;
	}

	// True the FIRST time this exact stack is seen in the process; false on every identical repeat. Records the stack so
	// a later identical occurrence is suppressed.
	bool firstTimeForStack(const void* const* frames, std::size_t count)
	{
		const std::lock_guard<std::mutex> lock(g_tracedStacksMutex);
		return g_tracedStacks.insert(hashStack(frames, count)).second;
	}
}    // namespace

//--------------------------------------------------------------------------------------------------
//	resetDeduplication ( public, static )
//--------------------------------------------------------------------------------------------------
void StackTrace::resetDeduplication() noexcept
{
	const std::lock_guard<std::mutex> lock(g_tracedStacksMutex);
	g_tracedStacks.clear();
}

//--------------------------------------------------------------------------------------------------
//	suppressed ( public )
//--------------------------------------------------------------------------------------------------
bool StackTrace::suppressed() const noexcept
{
	return m_suppressed;
}

//--------------------------------------------------------------------------------------------------
//	StackTrace ( public )
//--------------------------------------------------------------------------------------------------
StackTrace::StackTrace(unsigned int ignore /*= 0*/, bool deduplicateByStack /*= false*/)
{
	// Both DbgHelp and the BFD-backed symbolizer maintain process-global state and are not safe to call concurrently.
	static std::mutex stackTraceMutex;
	const std::lock_guard stackTraceLock(stackTraceMutex);
#ifdef WINDOWS
	const auto process = GetCurrentProcess();
	// One-time symbol handler setup, cached ONLY on success so a transient early failure (a module list that is not yet
	// stable during startup, a network/redirected PDB path that momentarily is not reachable) does not poison every
	// later trace for the process lifetime. fInvadeProcess is FALSE: a large host (a Qt GUI with 100+ loaded modules,
	// an embedded interpreter) makes the eager, synchronous whole-process module enumeration of TRUE fail and return
	// FALSE. Instead set SYMOPT_DEFERRED_LOADS so each module's symbols load lazily on the first SymFromAddr that needs
	// them, then SymRefreshModuleList to populate the module table for the modules already loaded. SymInitialize
	// returning FALSE with ERROR_INVALID_PARAMETER means the handler is already initialized for this process (another
	// component initialized it first) - that is success for our purposes.
	static const bool symbolsInitialized = []
	{
		SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
		const HANDLE self = GetCurrentProcess();
		if (SymInitialize(self, nullptr, FALSE) != FALSE || GetLastError() == ERROR_INVALID_PARAMETER)
		{
			SymRefreshModuleList(self);
			return true;
		}
		return false;
	}();
	if (!symbolsInitialized)
	{
		m_value = "<unable to initialize Windows symbols>\n";
		return;
	}
	void* stack[MAX_FRAMES]{};
	const unsigned short frames = CaptureStackBackTrace(ignore, MAX_FRAMES, stack, nullptr);
	// Gate the expensive per-frame symbolization behind a first-seen-stack check when deduplication is requested. The
	// raw addresses are already captured; a repeat of the exact same stack produces an empty, suppressed trace here and
	// never touches dbghelp again. A distinct stack (a different call path, even through the same source line) hashes
	// differently and falls through to a full trace.
	if (deduplicateByStack && !firstTimeForStack(stack, frames))
	{
		m_suppressed = true;
		return;
	}
	auto* rawSymbol = static_cast<SYMBOL_INFO*>(std::calloc(sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(char), 1));
	if (rawSymbol == nullptr)
	{
		m_value = "<unable to allocate Windows symbol buffer>\n";
		return;
	}
	const std::unique_ptr<SYMBOL_INFO, decltype(&std::free)> symbol(rawSymbol, &std::free);
	symbol->MaxNameLen   = MAX_SYM_NAME;
	symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
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

	// Gate the expensive symbolization behind a first-seen-stack check when deduplication is requested (identical to the
	// Windows branch): the raw addresses are captured, a repeat of the exact same stack is suppressed here and never
	// symbolized again, and a distinct call path always traces in full.
	if (deduplicateByStack && !firstTimeForStack(trace, static_cast<std::size_t>(frames)))
	{
		m_suppressed = true;
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
