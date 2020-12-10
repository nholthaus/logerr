//----------------------------
//  INCLUDES
//----------------------------

#include "timestampLite.h"

#if __has_include(<timezoneapi.h>)
#define WIN32_LEAN_AND_MEAN      // Exclude rarely-used stuff from Windows headers
#include <windows.h>
#include <timezoneapi.h>
#include <regex>
#endif

//--------------------------------------------------------------------------------------------------
//	TimestampLite
//--------------------------------------------------------------------------------------------------
TimestampLite::TimestampLite()
    : m_now(std::chrono::system_clock::now())
{
}

TimestampLite::operator std::time_t() const
{
	return std::chrono::system_clock::to_time_t(m_now);
}

TimestampLite::operator std::chrono::system_clock::time_point() const
{
	return m_now;
}

TimestampLite::operator std::string() const
{

#pragma warning(push)
#pragma warning(disable : 4996)    // disable: warning C4996: 'localtime': This function or variable may be unsafe. Consider using localtime_s instead. To disable deprecation, use _CRT_SECURE_NO_WARNINGS. See online help for details.
	char        buffer[128];
	auto now_c = static_cast<std::time_t>(*this);
	std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", std::localtime(&now_c));
#if __has_include(<timezoneapi.h>)
	// Windows + timezones is annoying
	DYNAMIC_TIME_ZONE_INFORMATION timeZoneInformation;
	GetDynamicTimeZoneInformation(&timeZoneInformation);
	std::wstring wtimezone = timeZoneInformation.DaylightName;
	std::string fulltimezone(wtimezone.begin(), wtimezone.end());
	std::regex rx(R"(\b(\w).*?\b)");
	std::smatch      match;
	std::string timezone;

	// get timezone abbreviation
	std::string::const_iterator searchStart( fulltimezone.cbegin() );
	while ( regex_search( searchStart, fulltimezone.cend(), match, rx ) )
	{
		timezone.append(match[1]);
		searchStart = match.suffix().first;
	}
#else
	char timezone[4];
	std::strftime(timezone, sizeof(timezone), "%Z", std::localtime(&now_c));
#endif

	std::string seconds(buffer);
	std::string nanoseconds = std::to_string(std::chrono::duration_cast<std::chrono::nanoseconds>(m_now.time_since_epoch()).count() % 1000000000);

	// have to add any leading zeros back in
	nanoseconds = std::string(9 - nanoseconds.length(), '0') + nanoseconds;

	return seconds + '.' + nanoseconds + ' ' + timezone;
#pragma warning(pop)
}
//----------------------------------------------------------------------------------------------------------------------
//  operator<<
//----------------------------------------------------------------------------------------------------------------------
std::ostream& operator<<(std::ostream& os, const TimestampLite& timestamp)
{
	os << static_cast<std::string>(timestamp);
	return os;
}

