//----------------------------
//  INCLUDES
//----------------------------

#include "timestampLite.h"

#include <ctime>
#include <cwctype>

#if __has_include(<timezoneapi.h>)
#define WIN32_LEAN_AND_MEAN    // Exclude rarely-used stuff from Windows headers
#include <windows.h>
#include <timezoneapi.h>
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
	char          buffer[128]{};
	const auto    now_c = static_cast<std::time_t>(*this);
	std::tm       localTime{};
#ifdef _WIN32
	if (localtime_s(&localTime, &now_c) != 0)
#else
	if (localtime_r(&now_c, &localTime) == nullptr)
#endif
		return {};
	if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &localTime) == 0)
		return {};
#if __has_include(<timezoneapi.h>)
	// Windows + timezones is annoying
	DYNAMIC_TIME_ZONE_INFORMATION timeZoneInformation{};
	const auto timeZoneId = GetDynamicTimeZoneInformation(&timeZoneInformation);
	const auto* timeZoneName = timeZoneId == TIME_ZONE_ID_DAYLIGHT ? timeZoneInformation.DaylightName
	                                                             : timeZoneInformation.StandardName;
	std::string timezone;
	bool        atWordStart = true;
	for (const auto* character = timeZoneName; *character != L'\0'; ++character)
	{
		if (std::iswspace(static_cast<std::wint_t>(*character)) != 0)
			atWordStart = true;
		else if (atWordStart)
		{
			if (*character <= 0x7f)
				timezone.push_back(static_cast<char>(*character));
			atWordStart = false;
		}
	}
#else
	char timezone[32]{};
	std::strftime(timezone, sizeof(timezone), "%Z", &localTime);
#endif

	const std::string seconds(buffer);
	std::string nanoseconds = std::to_string(std::chrono::duration_cast<std::chrono::nanoseconds>(m_now.time_since_epoch()).count() % 1000000000);

	// have to add any leading zeros back in
	nanoseconds = std::string(9 - nanoseconds.length(), '0') + nanoseconds;

	return seconds + '.' + nanoseconds + ' ' + timezone;
}
//----------------------------------------------------------------------------------------------------------------------
//  operator<<
//----------------------------------------------------------------------------------------------------------------------
std::ostream& operator<<(std::ostream& os, const TimestampLite& timestamp)
{
	os << static_cast<std::string>(timestamp);
	return os;
}
