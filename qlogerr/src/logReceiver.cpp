//----------------------------
//  INCLUDES
//----------------------------

#include "logReceiver.h"
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

	if (!socket.bind(QHostAddress::AnyIPv4, 52387, QUdpSocket::ShareAddress))
		RUN_ONCE_STARTED(ERR(std::string("Log Receiver failed to connect to port 52387: ").append(socket.errorString().toStdString())));
	
	socket.joinMulticastGroup(QHostAddress("239.239.239.239"));
	connect(&socket, &QUdpSocket::readyRead, this, &LogReceiver::processPendingDatagrams);
}

//----------------------------------------------------------------------------------------------------------------------
//      FUNCTION: processPendingDatagrams [private]
//----------------------------------------------------------------------------------------------------------------------
/// @brief writes logs to the console
//----------------------------------------------------------------------------------------------------------------------
void LogReceiver::processPendingDatagrams()
{
	QByteArray datagram;

	while (socket.hasPendingDatagrams())
	{
		QNetworkDatagram dgram = socket.receiveDatagram();
		std::string      logEntry(dgram.data().toStdString());
		emit             readyRead(logEntry);
	}
}
