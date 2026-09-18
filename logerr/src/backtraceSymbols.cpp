// attribution: https://oroboro.com/printing-stack-traces-file-line/

//----------------------------
//  INCLUDES
//----------------------------

#include "backtraceSymbols.h"

// C
#include <dwarf.h>
#include <elfutils/libdwfl.h>
#include <fcntl.h>
#include <unistd.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>

// std
#include <array>
#include <mutex>
#include <sstream>
#include <string>

// A stack trace is resolved with libdw (elfutils) rather than libbfd. libbfd's bfd_find_nearest_line builds a table over
// every compilation unit in the module before it answers anything, which costs ~9.6s of CPU on a 250MB executable and is
// paid again by every new process; libdw parses only the unit that contains the address and answers the same question in
// 0.12s.

// The debuginfod fallback in dwfl_standard_find_debuginfo is deliberately not used: for any module whose debug info is not
// present locally it blocks for the full connect timeout (90s by default), and DEBUGINFOD_URLS is set out of the box on
// Ubuntu and Fedora. A symbolizer that stalls the process it is diagnosing is worse than a slow one, so a separate debug
// file is searched for locally and a miss degrades to "??:0", never to a network wait.

// A frame's function, as far as this module can describe it.
struct ResolvedFunction
{
	std::string mName;               ///< empty when the module has neither debug info nor a symbol covering the address.
	Dwarf_Addr  mSymbolStart = 0;    ///< first address of the symbol covering it, and
	Dwarf_Addr  mSymbolEnd   = 0;    ///< one past its last - both 0 when no sized symbol covers the address.
};

struct Symbolizer
{
	Dwfl* mDwfl = nullptr;
	pid_t mPid  = 0;    ///< the process mDwfl holds modules for; a fork invalidates them.
};

static uint32_t         fileCrc(const char* path, bool& readable);
static bool             separateDebugFileMatches(const std::string& path, GElf_Word crc);
static ResolvedFunction functionAt(Dwfl_Module* module, Dwarf_Addr address);
static int              findDebuginfoLocally(Dwfl_Module*, void**, const char*, Dwarf_Addr, const char*, const char*, GElf_Word, char**);
static Symbolizer&      symbolizer();
static bool             readModuleList(Symbolizer& session, pid_t pid);
static Dwfl*            liveProcess();

static char*                theDebuginfoPath = nullptr;
static const Dwfl_Callbacks theCallbacks     = {
            .find_elf        = dwfl_linux_proc_find_elf,
            .find_debuginfo  = findDebuginfoLocally,
            .section_address = nullptr,
            .debuginfo_path  = &theDebuginfoPath,
};

//--------------------------------------------------------------------------------------------------
//	fileCrc (public ) [static ]
//--------------------------------------------------------------------------------------------------
// The CRC-32 a .gnu_debuglink section carries for its debug file, so a debug file left behind by an older build of the
// same binary is rejected instead of reporting line numbers out of it.
uint32_t fileCrc(const char* path, bool& readable)
{
	static const std::array<uint32_t, 256> table = []
	{
		std::array<uint32_t, 256> entries{};
		for (uint32_t i = 0; i < entries.size(); ++i)
		{
			uint32_t entry = i;
			for (int bit = 0; bit < 8; ++bit)
				entry = (entry & 1) ? (0xEDB88320u ^ (entry >> 1)) : (entry >> 1);
			entries[i] = entry;
		}
		return entries;
	}();

	readable = false;

	const int descriptor = open(path, O_RDONLY | O_CLOEXEC);
	if (descriptor < 0)
		return 0;

	uint32_t                         crc = 0xFFFFFFFFu;
	std::array<unsigned char, 65536> buffer{};
	ssize_t                          taken = 0;
	while ((taken = read(descriptor, buffer.data(), buffer.size())) > 0)
	{
		for (ssize_t i = 0; i < taken; ++i)
			crc = table[(crc ^ buffer[static_cast<size_t>(i)]) & 0xFF] ^ (crc >> 8);
	}

	close(descriptor);

	if (taken < 0)
		return 0;

	readable = true;
	return crc ^ 0xFFFFFFFFu;
}

//--------------------------------------------------------------------------------------------------
//	separateDebugFileMatches (public ) [static ]
//--------------------------------------------------------------------------------------------------
bool separateDebugFileMatches(const std::string& path, GElf_Word crc)
{
	bool           readable = false;
	const uint32_t actual   = fileCrc(path.c_str(), readable);
	return readable && actual == static_cast<uint32_t>(crc);
}

//--------------------------------------------------------------------------------------------------
//	findDebuginfoLocally (public ) [static ]
//--------------------------------------------------------------------------------------------------
// Find the separate debug file a stripped module points at, searching only the places it can be installed on this
// machine: the build-id store first, since a build id identifies the file it belongs to exactly, then the paths a
// .gnu_debuglink names. -1 means "no debug info for this module", which is what makes such a frame resolve to "??:0".
int findDebuginfoLocally(Dwfl_Module* module, void**, const char*, Dwarf_Addr, const char* fileName, const char* debuglinkFile,
                         GElf_Word debuglinkCrc, char** debuginfoFileName)
{
	const unsigned char* buildId       = nullptr;
	GElf_Addr            buildIdVaddr  = 0;
	const int            buildIdLength = module ? dwfl_module_build_id(module, &buildId, &buildIdVaddr) : 0;

	if (buildIdLength > 0 && buildId)
	{
		// /usr/lib/debug/.build-id/<first byte>/<remaining bytes>.debug - where a distribution's debug package installs
		// it, and where a stripped system library such as libc keeps the line numbers a trace needs.
		static constexpr char hex[] = "0123456789abcdef";

		std::string path("/usr/lib/debug/.build-id/");
		for (int i = 0; i < buildIdLength; ++i)
		{
			path.push_back(hex[buildId[i] >> 4]);
			path.push_back(hex[buildId[i] & 0x0F]);
			if (i == 0)
				path.push_back('/');
		}
		path.append(".debug");

		const int descriptor = open(path.c_str(), O_RDONLY | O_CLOEXEC);
		if (descriptor >= 0)
		{
			*debuginfoFileName = strdup(path.c_str());
			return descriptor;
		}
	}

	if (!fileName || !debuglinkFile || !*debuglinkFile)
		return -1;

	const std::string modulePath(fileName);
	const auto        separator = modulePath.rfind('/');
	const std::string directory = (separator == std::string::npos) ? std::string(".") : modulePath.substr(0, separator);

	const std::string candidates[] = {
	        directory + "/" + debuglinkFile,
	        directory + "/.debug/" + debuglinkFile,
	        "/usr/lib/debug" + directory + "/" + debuglinkFile,
	};

	for (const std::string& candidate : candidates)
	{
		if (!separateDebugFileMatches(candidate, debuglinkCrc))
			continue;

		const int descriptor = open(candidate.c_str(), O_RDONLY | O_CLOEXEC);
		if (descriptor < 0)
			continue;

		*debuginfoFileName = strdup(candidate.c_str());
		return descriptor;
	}

	return -1;
}

//--------------------------------------------------------------------------------------------------
//	functionAt (public ) [static ]
//--------------------------------------------------------------------------------------------------
// The function an address belongs to, and the bounds of the symbol covering it.
//
// The name is taken from the innermost DWARF function covering the address, an inlined one included, so the name agrees
// with the source line reported beside it: where a callee was inlined, the line table gives the CALLEE's line, and naming
// the frame after the caller it was folded into would describe two different functions on one line. Preference among the
// names that function may have:
//
//   1. its DWARF linkage name, the mangled form, which demangles into a full signature;
//   2. the symbol-table name, but ONLY when that symbol BEGINS at this function. A local function has no symbol of its
//      own, so the symbol covering it is the larger exported one it sits inside, and naming the frame after that is
//      simply wrong;
//   3. the short DWARF name, which is what such a local function has.
//
// The scope walk is best effort: it reaches a function DIE by pc range, and a lambda's operator() hangs off its closure
// CLASS, which has no range to descend through. Such a frame falls to its symbol, which for a lambda is the mangled
// operator() and is exactly right.
//
// The symbol bounds come back with the name because they bound the source lookup too. dwfl answers "nearest line" for any
// address, so an address in code no line table describes - the entry stub, anything linked in without debug info - is
// answered with the nearest line in some unrelated unit: a plausible file and line belonging to another function
// entirely. A line is believed only when it falls inside the same symbol as the address.
ResolvedFunction functionAt(Dwfl_Module* module, Dwarf_Addr address)
{
	ResolvedFunction resolved;

	Dwarf_Addr bias    = 0;
	Dwarf_Die* unitDie = dwfl_module_addrdie(module, address, &bias);

	std::string linkageName;
	std::string shortName;
	Dwarf_Addr  functionStart = 0;

	if (unitDie)
	{
		Dwarf_Die* scopes     = nullptr;
		const int  scopeCount = dwarf_getscopes(unitDie, address - bias, &scopes);

		// dwarf_getscopes hands back the scopes innermost-first, so the first function-like scope is the one to name.
		for (int i = 0; i < scopeCount; ++i)
		{
			const int tag = dwarf_tag(&scopes[i]);
			if (tag != DW_TAG_subprogram && tag != DW_TAG_inlined_subroutine && tag != DW_TAG_entry_point)
				continue;

			Dwarf_Attribute attribute;
			if (dwarf_attr_integrate(&scopes[i], DW_AT_linkage_name, &attribute))
			{
				if (const char* name = dwarf_formstring(&attribute))
					linkageName = name;
			}
			if (const char* name = dwarf_diename(&scopes[i]))
				shortName = name;

			Dwarf_Addr lowpc = 0;
			if (dwarf_lowpc(&scopes[i], &lowpc) == 0)
				functionStart = lowpc + bias;

			break;
		}

		free(scopes);
	}

	GElf_Off    symbolOffset = 0;
	GElf_Sym    symbol{};
	const char* symbolName = dwfl_module_addrinfo(module, address, &symbolOffset, &symbol, nullptr, nullptr, nullptr);

	if (symbolName && symbol.st_size > 0)
	{
		resolved.mSymbolStart = address - symbolOffset;
		resolved.mSymbolEnd   = resolved.mSymbolStart + symbol.st_size;
	}

	if (!linkageName.empty())
	{
		resolved.mName = linkageName;
		return resolved;
	}

	const bool symbolIsThisFunction = symbolName && functionStart != 0 && (address - symbolOffset) == functionStart;

	if (symbolIsThisFunction || shortName.empty())
		resolved.mName = symbolName ? symbolName : std::string();
	else
		resolved.mName = shortName;

	return resolved;
}

//--------------------------------------------------------------------------------------------------
//	symbolizer (public ) [static ]
//--------------------------------------------------------------------------------------------------
// The session for this process. It is INTENTIONALLY LEAKED (never destroyed) for the same reason the module cache before
// it was: a trace can be symbolized on the async trace worker, or on any thread during exit-time teardown, after a
// function-local static would already have been destroyed, and a destroyed session touched by a still-running symbolizer
// is a use-after-free.
Symbolizer& symbolizer()
{
	static Symbolizer& session = *new Symbolizer;
	return session;
}

//--------------------------------------------------------------------------------------------------
//	readModuleList (public ) [static ]
//--------------------------------------------------------------------------------------------------
// Read the process's loaded modules into the session. Also the refresh path: a library dlopen'd since the last read is
// picked up by reading the list again.
bool readModuleList(Symbolizer& session, pid_t pid)
{
	if (!session.mDwfl)
		return false;

	dwfl_report_begin(session.mDwfl);
	const bool read = dwfl_linux_proc_report(session.mDwfl, pid) == 0;
	dwfl_report_end(session.mDwfl, nullptr, nullptr);

	session.mPid = read ? pid : 0;
	return read;
}

//--------------------------------------------------------------------------------------------------
//	liveProcess (public ) [static ]
//--------------------------------------------------------------------------------------------------
Dwfl* liveProcess()
{
	Symbolizer& session = symbolizer();

	const pid_t pid = getpid();
	if (session.mDwfl && session.mPid == pid)
		return session.mDwfl;

	if (!session.mDwfl)
		session.mDwfl = dwfl_begin(&theCallbacks);

	// A new session, or one inherited across a fork whose modules belong to another process image.
	return readModuleList(session, pid) ? session.mDwfl : nullptr;
}

//--------------------------------------------------------------------------------------------------
//	moduleContaining (public ) [static ]
//--------------------------------------------------------------------------------------------------
// The module an address really falls inside.
//
// dwfl_addrmodule alone is not enough: asked about an address past the end of every module it knows, it answers with the
// nearest one BELOW rather than with nothing. Against a module list that has gone stale - a library loaded since it was
// read - that is a non-null answer for a module the address is nowhere near, so the caller cannot tell a stale list from
// a resolved one, and the frame resolves against the wrong file. Whether it happens at all depends on where the loader
// happened to place the new library, which makes it an intermittent wrong answer rather than an honest failure.
Dwfl_Module* moduleContaining(Dwfl* dwfl, Dwarf_Addr address)
{
	Dwfl_Module* module = dwfl ? dwfl_addrmodule(dwfl, address) : nullptr;
	if (!module)
		return nullptr;

	Dwarf_Addr start = 0;
	Dwarf_Addr end   = 0;
	if (!dwfl_module_info(module, nullptr, &start, &end, nullptr, nullptr, nullptr, nullptr))
		return nullptr;

	return (address >= start && address < end) ? module : nullptr;
}

//--------------------------------------------------------------------------------------------------
//	backtraceSymbols (public ) []
//--------------------------------------------------------------------------------------------------
std::vector<std::pair<std::string, std::string>> backtraceSymbols(void* const* addrList, int numAddr)
{
	// A libdw session is not thread safe, and nothing below may log: a LOGERR raised inside the symbolizer would
	// symbolize its own trace and deadlock on this mutex. Anything unreadable degrades to "??:0" instead.
	static std::mutex& symbolizerMutex = *new std::mutex;

	const std::lock_guard<std::mutex> lock(symbolizerMutex);

	std::vector<std::pair<std::string, std::string>> symbols;
	if (numAddr <= 0)
		return symbols;
	symbols.reserve(static_cast<size_t>(numAddr));

	Dwfl* dwfl        = liveProcess();
	bool  listRefresh = false;

	for (int i = 0; i < numAddr; i++)
	{
		const auto address = reinterpret_cast<Dwarf_Addr>(addrList[i]);

		Dwfl_Module* module = moduleContaining(dwfl, address);

		// An address inside no known module is the signature of a library dlopen'd since the module list was read. Read it
		// again, once per trace, and look for the module a second time.
		if (!module && dwfl && !listRefresh)
		{
			listRefresh = true;
			if (readModuleList(symbolizer(), getpid()))
				module = moduleContaining(dwfl, address);
		}

		std::string functionName;
		std::string filename;
		int         line = 0;

		if (module)
		{
			const ResolvedFunction resolved = functionAt(module, address);
			functionName                    = resolved.mName;

			if (Dwfl_Line* sourceLine = dwfl_module_getsrc(module, address))
			{
				Dwarf_Addr lineAddress = 0;
				if (const char* source = dwfl_lineinfo(sourceLine, &lineAddress, &line, nullptr, nullptr, nullptr))
				{
					const bool lineIsInThisFunction =
					        resolved.mSymbolEnd == 0 || (lineAddress >= resolved.mSymbolStart && lineAddress < resolved.mSymbolEnd);
					if (lineIsInThisFunction)
						filename = source;
				}
			}
		}

		if (functionName.empty() && filename.empty())
		{
			// Nothing resolved: report the address itself, relative to its module when there is one, so the frame keeps its
			// place in the trace instead of dropping out of it.
			Dwarf_Addr bias = 0;
			if (module)
				dwfl_module_info(module, nullptr, nullptr, nullptr, &bias, nullptr, nullptr, nullptr);

			std::stringstream unresolved;
			unresolved << "[0x" << std::hex << (bias <= address ? address - bias : address) << "]";
			symbols.emplace_back("??:0", unresolved.str());
			continue;
		}

		if (functionName.empty())
			functionName = "??";

		if (!filename.empty())
		{
			// The trace carries the base name, not the compiler's path to it.
			std::stringstream source(filename);
			while (getline(source, filename, '/')) {};
			filename.append(":").append(std::to_string(line));
		}

		symbols.emplace_back(filename, functionName);
	}

	return symbols;
}
