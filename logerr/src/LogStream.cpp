//------------------------------
//	INCLUDES
//------------------------------

#include <LogStream.h>
#include <ostream>

thread_local std::string LogStream::m_string;

//--------------------------------------------------------------------------------------------------
//	LogStream () []
//--------------------------------------------------------------------------------------------------
LogStream::LogStream(std::ostream& stream) 
	: m_stream(stream)
	, m_old_buf(stream.rdbuf())
{
	stream.rdbuf(this);
}

//--------------------------------------------------------------------------------------------------
//	~LogStream () []
//--------------------------------------------------------------------------------------------------
LogStream::~LogStream() noexcept
{
	// output anything that is left
	try
	{
		if (!m_string.empty())
			log();
	}
	catch (...)    // NOLINT(bugprone-empty-catch)
	{
		// A stream buffer destructor cannot propagate failures from user callbacks.
	}

	try
	{
		m_stream.rdbuf(m_old_buf);
	}
	catch (...)    // NOLINT(bugprone-empty-catch)
	{
		// std::basic_ios::rdbuf() can throw when the stream has an exception mask.
	}
}

//--------------------------------------------------------------------------------------------------
//	overflow () []
//--------------------------------------------------------------------------------------------------
std::basic_streambuf<char>::int_type LogStream::overflow(int_type v)
{
	if (traits_type::eq_int_type(v, traits_type::eof()))
	{
		return traits_type::not_eof(v);
	}

	m_string += traits_type::to_char_type(v);

	if (v == '\n')
		log();

	return v;
}

//--------------------------------------------------------------------------------------------------
//	xsputn (protected ) [virtual ]
//--------------------------------------------------------------------------------------------------
std::streamsize LogStream::xsputn(const char* p, std::streamsize n)
{
	if (n <= 0)
	{
		return 0;
	}

	m_string.append(p, p + n);

	if (m_string.back() == '\n')
		log();

	return n;
}

//----------------------------------------------------------------------------------------------------------------------
//  log (protected)
//----------------------------------------------------------------------------------------------------------------------
void LogStream::log()
{
	const std::lock_guard lock(m_callbackMutex);
	try
	{
		for(auto& [name, callback] : m_callbacks)
		{
			callback(m_string);
		}
	}
	catch (...)
	{
		m_string.clear();
		throw;
	}
	m_string.clear();
}

//----------------------------------------------------------------------------------------------------------------------
//  unregisterCallbacks (protected)
//----------------------------------------------------------------------------------------------------------------------
void LogStream::unregisterLogFunction(const std::string& name /*= ""*/)
{
	const std::lock_guard lock(m_callbackMutex);
	if(name.empty())
		m_callbacks.clear();
	else
		m_callbacks.erase(name);
}
