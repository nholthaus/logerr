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
LogStream::~LogStream()
{
	// output anything that is left
	if (!m_string.empty())
		log();

	m_stream.rdbuf(m_old_buf);
}

//--------------------------------------------------------------------------------------------------
//	overflow () []
//--------------------------------------------------------------------------------------------------
std::basic_streambuf<char>::int_type LogStream::overflow(int_type v)
{
	m_string += static_cast<char>(v);

	if (v == '\n')
		log();

	return v;
}

//--------------------------------------------------------------------------------------------------
//	xsputn (protected ) [virtual ]
//--------------------------------------------------------------------------------------------------
std::streamsize LogStream::xsputn(const char* p, std::streamsize n)
{
	m_string.append(p, p + n);

	if (*(--m_string.end()) == '\n')
		log();

	return n;
}

//----------------------------------------------------------------------------------------------------------------------
//  log (protected)
//----------------------------------------------------------------------------------------------------------------------
void LogStream::log()
{
	std::unique_lock<std::mutex> lock(m_callbackMutex);
	for(auto& [name, callback] : m_callbacks)
	{
		callback(m_string);
	}
	m_string.clear();
}

//----------------------------------------------------------------------------------------------------------------------
//  unregisterCallbacks (protected)
//----------------------------------------------------------------------------------------------------------------------
void LogStream::unregisterLogFunction(const std::string& name /*= ""*/)
{
	std::unique_lock<std::mutex> lock(m_callbackMutex);
	if(name.empty())
		m_callbacks.clear();
	else
		m_callbacks.erase(name);
}
