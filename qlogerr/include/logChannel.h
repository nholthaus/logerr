#ifndef LOGCHANNEL_H
#define LOGCHANNEL_H

//----------------------------
//  INCLUDES
//----------------------------

#include <QByteArray>
#include <QHostAddress>
#include <QString>

//----------------------------------------------------------------------------------------------------------------------
//      NAMESPACE: LogChannel
//----------------------------------------------------------------------------------------------------------------------
/// @brief   The UDP multicast rendezvous the log blaster/receiver share. The defaults (239.239.239.239:52387) are the
///          well-known channel; they are OPTIONALLY overridable via environment variables so multiple independent
///          fleets — or a machine already using 52387 — can pick a private channel without a rebuild:
///             LOGERR_LOG_PORT   — UDP port  (1..65535; invalid/unset → 52387)
///             LOGERR_LOG_GROUP  — multicast group address (unset → 239.239.239.239)
///          A blaster and receiver on the same host must, of course, agree — set the same values for both.
//----------------------------------------------------------------------------------------------------------------------
namespace LogChannel
{
	inline constexpr quint16 kDefaultPort = 52387;
	inline const QString      kDefaultGroup = QStringLiteral("239.239.239.239");

	/// The log channel port: $LOGERR_LOG_PORT when set to a valid 1..65535, else the default 52387.
	inline quint16 port()
	{
		bool          ok    = false;
		const int     value = qEnvironmentVariableIntValue("LOGERR_LOG_PORT", &ok);
		if (ok && value > 0 && value <= 65535)
			return static_cast<quint16>(value);
		return kDefaultPort;
	}

	/// The multicast group: $LOGERR_LOG_GROUP when set, else 239.239.239.239.
	inline QHostAddress group()
	{
		const QString override = qEnvironmentVariable("LOGERR_LOG_GROUP");
		return override.isEmpty() ? QHostAddress(kDefaultGroup) : QHostAddress(override);
	}
}    // namespace LogChannel

#endif    // LOGCHANNEL_H
