
//----------------------------
//  INCLUDES
//----------------------------

#include <StackTraceException.h>
#ifdef BUILD_WITH_QT
#include <QString>
#include <../../qlogerr/include/qappinfo.h>
#else
#include <appinfo.h>
#endif

#include <sstream>
#include <utility>

//--------------------------------------------------------------------------------------------------
//	StackTraceException (public ) []
//--------------------------------------------------------------------------------------------------
StackTraceException::StackTraceException(std::string errorMessage, std::string filename, std::string function, size_t line, bool fatal)
    : m_errorMessage(std::move(errorMessage))
    , m_fileName(std::move(filename))
    , m_function(std::move(function))
    , m_line(line)
    , m_trace(StackTrace(1))
    , m_fatal(fatal)
{
	std::ostringstream whatStream;
	whatStream << m_errorMessage << std::endl
	           << "in `" << m_function << "` at `" << m_fileName << ":" << std::to_string(line) << "`"
	           << std::endl
	           << std::endl
#ifndef BUILD_WITH_QT
	           << APPINFO::systemDetails()
#else
	           << QAPPINFO::systemDetails().toStdString()
#endif
	           << "STACK TRACE:"
	           << std::endl
	           << std::endl
	           << m_trace.data();
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
