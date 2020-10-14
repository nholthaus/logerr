//------------------------------
//	INCLDUES
//------------------------------
#include <LogStream.h>
#include <ostream>

Q_DECLARE_METATYPE(std::string)

//--------------------------------------------------------------------------------------------------
//	LogStream () []
//--------------------------------------------------------------------------------------------------
LogStream::LogStream(std::ostream& stream) 
	: m_stream(stream)
	, m_old_buf(stream.rdbuf())
{
	qRegisterMetaType<std::string>("std::string");
	stream.rdbuf(this);
}

//--------------------------------------------------------------------------------------------------
//	~LogStream () []
//--------------------------------------------------------------------------------------------------
LogStream::~LogStream()
{
	// output anything that is left
	if (!m_string.localData().empty())
		emit logEntryReady(m_string.localData());

	m_stream.rdbuf(m_old_buf);
}

//--------------------------------------------------------------------------------------------------
//	overflow () []
//--------------------------------------------------------------------------------------------------
std::basic_streambuf<char>::int_type LogStream::overflow(int_type v)
{
	m_string.localData() += v;

	if (v == '\n')
	{
		emit logEntryReady(m_string.localData());
		m_string.localData().clear();
	}

	return v;
}

//--------------------------------------------------------------------------------------------------
//	xsputn (protected ) [virtual ]
//--------------------------------------------------------------------------------------------------
std::streamsize LogStream::xsputn(const char* p, std::streamsize n)
{
	m_string.localData().append(p, p + n);

	if (*(--m_string.localData().end()) == '\n')
	{
		emit logEntryReady(m_string.localData());
		m_string.localData().clear();
	}

	return n;
}

