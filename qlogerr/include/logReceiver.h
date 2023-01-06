#ifndef LOGRECEIVER_H
#define LOGRECEIVER_H

//----------------------------
//  INCLUDES
//----------------------------

#include <QHostAddress>
#include <QUdpSocket>

//----------------------------------------------------------------------------------------------------------------------
//      CLASS: Receiver
//----------------------------------------------------------------------------------------------------------------------
/// @brief Class that received UDP log datagrams and writes them to the console
//----------------------------------------------------------------------------------------------------------------------
class LogReceiver : public QObject
{
	Q_OBJECT

public:
	explicit LogReceiver(QObject* parent = nullptr);

signals:
	void readyRead(std::string str);

private slots:
	void processPendingDatagrams();

private:
	QUdpSocket socket;
};

#endif    // LOGRECEIVER_H