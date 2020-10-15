
//----------------------------
//  INCLUDES
//----------------------------

#include <StackTraceException.h>
#include <appinfo.h>
#include <appinfo.h>

#include <iomanip>
#include <iostream>

//--------------------------------------------------------------------------------------------------
//	StackTraceException (public ) []
//--------------------------------------------------------------------------------------------------
StackTraceException::StackTraceException(const std::string& errorMessage, const std::string& filename, const std::string& function, size_t line, bool fatal)
	: m_errorMessage(errorMessage)
	, m_fileName(filename)
	, m_function(function)
	, m_line(line)
	, m_trace(StackTrace(1))
	, m_fatal(fatal)
{
	std::ostringstream whatStream;
	whatStream << m_errorMessage << std::endl << "in `" << m_function << "` at `" << m_fileName << ":" << std::to_string(line)
	        << std::endl << std::endl << APPINFO::systemDetails() << "STACK TRACE:" << std::endl << std::endl << m_trace;
	m_what = std::move(whatStream.str());

	if (fatal)
		m_what.insert(0, "FATAL ");
}

//--------------------------------------------------------------------------------------------------
//	what (public ) []
//--------------------------------------------------------------------------------------------------
char const* StackTraceException::what() const noexcept
{
	return m_what.data();
}

//--------------------------------------------------------------------------------------------------
//	filename (public ) []
//--------------------------------------------------------------------------------------------------
std::string StackTraceException::filename() const
{
	return m_fileName;
}

//--------------------------------------------------------------------------------------------------
//	errorMessage (public ) []
//--------------------------------------------------------------------------------------------------
std::string StackTraceException::errorMessage() const
{
	return m_errorMessage;
}

//--------------------------------------------------------------------------------------------------
//	errorDetails (public ) []
//--------------------------------------------------------------------------------------------------
std::string StackTraceException::errorDetails() const
{
	return m_what;
}

//--------------------------------------------------------------------------------------------------
//	function () []
//--------------------------------------------------------------------------------------------------
std::string StackTraceException::function() const
{
	return m_function;
}

//--------------------------------------------------------------------------------------------------
//	line (public ) []
//--------------------------------------------------------------------------------------------------
size_t StackTraceException::line() const
{
	return m_line;
}

//--------------------------------------------------------------------------------------------------
//	trace (public ) []
//--------------------------------------------------------------------------------------------------
std::string StackTraceException::trace() const
{
	return m_trace;
}

//--------------------------------------------------------------------------------------------------
//	fatal (public ) []
//--------------------------------------------------------------------------------------------------
bool StackTraceException::fatal() const
{
	return m_fatal;
}

