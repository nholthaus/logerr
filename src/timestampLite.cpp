#include "timestampLite.h"

#include <QDateTime>
#include <QDebug>
#include <QString>

//--------------------------------------------------------------------------------------------------
//	TimestampLite (private ) []
//--------------------------------------------------------------------------------------------------
TimestampLite::TimestampLite() : m_now(std::chrono::system_clock::now())
{

}

//--------------------------------------------------------------------------------------------------
//	update (public ) []
//--------------------------------------------------------------------------------------------------
TimestampLite& TimestampLite::update()
{
	m_now = std::chrono::system_clock::now();
	return *this;
}

//--------------------------------------------------------------------------------------------------
//	fractionalPart (public ) []
//--------------------------------------------------------------------------------------------------
std::chrono::milliseconds TimestampLite::fractionalPart() const
{
	return std::chrono::milliseconds(this->to<uint64_t>() % 1000);
}

//--------------------------------------------------------------------------------------------------
//	seconds_since_epoch (public ) []
//--------------------------------------------------------------------------------------------------
std::chrono::seconds TimestampLite::seconds_since_epoch() const
{
	return std::chrono::duration_cast<std::chrono::seconds>(m_now.time_since_epoch());
}

//--------------------------------------------------------------------------------------------------
//	operator QDateTime (public ) []
//--------------------------------------------------------------------------------------------------
TimestampLite::operator QDateTime() const
{
	return QDateTime::fromMSecsSinceEpoch((std::chrono::duration_cast<std::chrono::milliseconds>(m_now.time_since_epoch()).count()));
}

//--------------------------------------------------------------------------------------------------
//	operator QString (public ) []
//--------------------------------------------------------------------------------------------------
TimestampLite::operator QString() const
{
	QString nanoseconds = QString("%1").arg(std::chrono::duration_cast<std::chrono::nanoseconds>(m_now.time_since_epoch()).count() % 1000000000, 9, 10, QChar('0'));
	return  static_cast<QDateTime>(*this).toString("yyyy-MM-dd hh:mm:ss.") + nanoseconds;
}

//--------------------------------------------------------------------------------------------------
//	operator<< (public ) []
//--------------------------------------------------------------------------------------------------
std::ostream& operator<<(std::ostream& os, const TimestampLite& t)
{
	os << static_cast<std::string>(t);
	return os;
}

//--------------------------------------------------------------------------------------------------
//	operator std::uint64_t (public ) []
//--------------------------------------------------------------------------------------------------
TimestampLite::operator std::uint64_t() const
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(m_now.time_since_epoch()).count();
}

//--------------------------------------------------------------------------------------------------
//	operator std::chrono::milliseconds (public ) []
//--------------------------------------------------------------------------------------------------
TimestampLite::operator std::chrono::milliseconds() const
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(m_now.time_since_epoch());
}

//--------------------------------------------------------------------------------------------------
//	operator std::string () []
//--------------------------------------------------------------------------------------------------
TimestampLite::operator std::string() const
{
#pragma warning(push)
#pragma warning(disable: 4996)	// disable: warning C4996: 'localtime': This function or variable may be unsafe. Consider using localtime_s instead. To disable deprecation, use _CRT_SECURE_NO_WARNINGS. See online help for details.	
	char buffer[128];
	std::time_t now_c = *this;
	std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", std::localtime(&now_c));

	std::string seconds(buffer);
	std::string nanoseconds = std::to_string(std::chrono::duration_cast<std::chrono::nanoseconds>(m_now.time_since_epoch()).count() % 1000000000);

	// have to add any leading zeros back in
	nanoseconds = std::string(9 - nanoseconds.length(), '0') + nanoseconds;

	return seconds + '.' + nanoseconds;
#pragma warning(pop)
}

//--------------------------------------------------------------------------------------------------
//	operator std::chrono::system_clock::time_point (public ) []
//--------------------------------------------------------------------------------------------------
TimestampLite::operator std::chrono::system_clock::time_point() const
{
	return m_now;
}

//--------------------------------------------------------------------------------------------------
//	operator std::time_t (public ) []
//--------------------------------------------------------------------------------------------------
TimestampLite::operator std::time_t() const
{
	return std::chrono::system_clock::to_time_t(m_now);
}

//--------------------------------------------------------------------------------------------------
//	operator<< (public ) []
//--------------------------------------------------------------------------------------------------
QDebug operator<<(QDebug d, TimestampLite t)
{
	d << static_cast<QString>(t);
	return d;
}
