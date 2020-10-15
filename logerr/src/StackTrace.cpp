//------------------------
//	INCLUDES
//------------------------
#include "StackTrace.h"
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

// The intent is for this to be defined (or not) by CMake. If you're not using CMake, define this
// yourself (maybe based on the _MSV_VER or __GNUC__ macros)
#ifdef _MSC_VER
#include <windows.h>
#pragma warning(push)
#pragma warning(disable : 4091)
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#pragma warning(pop)
#else
#include <cxxabi.h>
#include <backtraceSymbols.h>
#include <stdlib.h>
#endif    // _MSC_VER

//--------------------------------------------------------------------------------------------------
//	StackTrace ( public )
//--------------------------------------------------------------------------------------------------
StackTrace::StackTrace(unsigned int ignore /*= 0*/)
{
#ifdef _MSC_VER
	unsigned int    i;
	void*           stack[MAX_FRAMES];
	unsigned short  frames;
	SYMBOL_INFO*    symbol;
	HANDLE          process;
	DWORD           dwDisplacement;
	IMAGEHLP_LINE64 line;
	std::string     filename;
	unsigned int    lineNumber;

	process = GetCurrentProcess();

	SymInitialize(process, nullptr, TRUE);

	frames               = CaptureStackBackTrace(ignore, MAX_FRAMES, stack, nullptr);
	symbol               = (SYMBOL_INFO*) calloc(sizeof(SYMBOL_INFO) + 256 * sizeof(char), 1);
	symbol->MaxNameLen   = 255;
	symbol->SizeOfStruct = sizeof(SYMBOL_INFO);

	SymSetOptions(SYMOPT_LOAD_LINES);
	line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);

	std::ostringstream value;

	for (i = 1; i < frames; i++)
	{
		SymFromAddr(process, (DWORD64)(stack[i]), nullptr, symbol);
		if (SymGetLineFromAddr64(process, symbol->Address, &dwDisplacement, &line))
		{
			filename   = line.FileName;
			lineNumber = line.LineNumber;
		}
		else
		{
			filename   = "??";
			lineNumber = 0;
		}

		value << std::right << std::setw(5)<< "["
		      << std::left << std::setw(0) << (i - 1)
		      << std::left << std::setw(4) << "]"
		      << std::left << std::setw(0) << "0x"
		      << std::right << std::setw(16) << std::setfill('0') << symbol->Address
		      << std::left << std::setw(0) << ": "
		      << std::left << std::setw(60) << std::setfill(' ') << symbol->Name
		      << std::left << std::setw(0) << "| "
		      << filename << ':' << lineNumber
		      << std::endl;
	}

	m_value = std::move(value.str());
	free(symbol);

#else
	// storage array for stack trace address data
	void* addrlist[MAX_FRAMES];

	// retrieve current stack addresses
	int addrlen = backtrace(addrlist, sizeof(addrlist) / sizeof(void*));

	if (addrlen == 0)
	{
		m_value.append("<empty, possibly corrupt>\n");
	}

	// resolve addresses into strings containing "filename(function+address)",
	// this array must be free()-ed
	char** symbollist = backtrace_symbols(addrlist, addrlen);

	// allocate string which will be filled with the de-mangled function name
	char* funcname = new char[FUNCTION_NAME_SIZE];

	std::ostringstream value;

	// iterate over the returned symbol lines. skip the first, it is the
	// address of this function.
	for (int i = 1 + ignore; i < addrlen; i++)
	{
		char *begin_name = 0, *begin_offset = 0, *end_offset = 0;

		// find parentheses and +address offset surrounding the mangled name:
		// ./module(function+0x15c) [0x8048a6d]
		for (char* p = symbollist[i]; *p; ++p)
		{
			if (*p == '(')
				begin_name = p;
			else if (*p == '+')
				begin_offset = p;
			else if (*p == ')' && begin_offset)
			{
				end_offset = p;
				break;
			}
		}

		if (begin_name && begin_offset && end_offset && begin_name < begin_offset)
		{
			*begin_name++   = '\0';
			*begin_offset++ = '\0';
			*end_offset     = '\0';

			// mangled name is now in [begin_name, begin_offset) and caller
			// offset in [begin_offset, end_offset). now apply
			// __cxa_demangle():

			int    status;
			size_t nameLength;

			// if you see "Error: invalid pointer" here you probably need to`
			// increase the size of FUNCTION_NAME_SIZE.
			char* ret = abi::__cxa_demangle(begin_name,
			                                funcname,
			                                &nameLength,
			                                &status);

			if (status == 0)
			{
				funcname = ret;    // use possibly realloc()-ed string
				value << std::right << std::setw(5)<< "["
				      << std::left << std::setw(0) << (i - ignore)
				      << std::left << std::setw(4) << "]"
				      << std::left << std::setw(0) << "0x"
				      << std::right << std::setw(16) << std::setfill('0') << (unsigned long long) addrlist[i]
				      << std::left << std::setw(0) << ": "
				      << std::left << std::setw(60) << std::setfill(' ') << funcname
				      << std::left << std::setw(0) << "| ";
			}
			else
			{
				// de-mangling failed. Output function name as a C function with
				// no arguments.
				value << std::right << std::setw(5)<< "["
				      << std::left << std::setw(0) << (i - ignore)
				      << std::left << std::setw(4) << "]"
				      << std::left << std::setw(0) << "0x"
				      << std::right << std::setw(16) << std::setfill('0') << (unsigned long long) addrlist[i]
				      << std::left << std::setw(0) << ": "
				      << std::left << std::setw(60) << std::setfill(' ') << begin_name
				      << std::left << std::setw(0) << "| ";
			}
		}
		else
		{
			// couldn't parse the line? print the whole line.
			value << std::right << std::setw(5)<< "["
			      << std::left << std::setw(0) << (i - ignore)
			      << std::left << std::setw(4) << "]"
			      << std::left << std::setw(0) << "0x"
			      << std::right << std::setw(16) << std::setfill('0') << (unsigned long long) addrlist[i]
			      << std::left << std::setw(0) << ": "
			      << std::left << std::setw(60) << std::setfill(' ') << symbollist[i]
			      << std::left << std::setw(0) << "| ";
		}

		// get the file name associated with the symbol
		char** bts = backtraceSymbols(&addrlist[i], 1);
		value << bts[0] << std::endl;
		free(bts);
	}

	// store values
	m_value = value.str();

	// Clean up
	delete[] funcname;
	free(symbollist);
#endif
}

//--------------------------------------------------------------------------------------------------
//  string ( public )
//--------------------------------------------------------------------------------------------------
StackTrace::operator std::string() const
{
	return m_value;
}

//----------------------------------------------------------------------------------------------------------------------
//  char* ( public )
//----------------------------------------------------------------------------------------------------------------------
StackTrace::operator const char*() const
{
	return m_value.data();
}
