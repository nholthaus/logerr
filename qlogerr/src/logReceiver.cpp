//----------------------------
//  INCLUDES
//----------------------------

#include "logReceiver.h"
#include "logChannel.h"
#include "logerrGuiApplication.h"

#include <logerr>

#include <QNetworkDatagram>
#include <iostream>

//----------------------------------------------------------------------------------------------------------------------
//      FUNCTION: CONSTRUCTOR [public]
//----------------------------------------------------------------------------------------------------------------------
/// @brief Constructor
/// @param parent Qt parent object
//----------------------------------------------------------------------------------------------------------------------
LogReceiver::LogReceiver(QObject* parent)
    : QObject(parent)
{
	qRegisterMetaType<std::string>();

	// The log channel is a well-known, SHARED multicast rendezvous: any number of processes bind it so each can
	// receive the log stream. Port/group default to 239.239.239.239:52387 and are optionally overridable via
	// environment (see logChannel.h). ShareAddress alone is a no-op on Windows (it needs SO_REUSEADDR) — so a second
	// process would fail to bind. ReuseAddressHint sets SO_REUSEADDR, letting every listener share the port portably.
	if (const quint16 port = LogChannel::port(); !socket.bind(QHostAddress::AnyIPv4, port, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint))
	{
		// Non-fatal: the multicast log VIEW is optional (this process's own logs still reach its dock/file directly).
		// A failed bind must never disrupt the application, so warn rather than throw — another listener may simply
		// already hold the socket, which is expected when multiple instances run.
		const std::string reason = socket.errorString().toStdString();
		RUN_ONCE_STARTED(LOGWARNING << "Log Receiver could not bind the shared log port " << port << " (" << reason
		                            << "); cross-process log view disabled for this instance." << ENDL;);
	}

	socket.joinMulticastGroup(LogChannel::group());
	connect(&socket, &QUdpSocket::readyRead, this, &LogReceiver::processPendingDatagrams);
}

//----------------------------------------------------------------------------------------------------------------------
//      FUNCTION: processPendingDatagrams [private]
//----------------------------------------------------------------------------------------------------------------------
/// @brief writes logs to the console
//----------------------------------------------------------------------------------------------------------------------
void LogReceiver::processPendingDatagrams()
{
	while (socket.hasPendingDatagrams())
	{
		const QNetworkDatagram dgram = socket.receiveDatagram();
		const std::string logEntry(dgram.data().toStdString());
		emit              readyRead(logEntry);
	}
}
