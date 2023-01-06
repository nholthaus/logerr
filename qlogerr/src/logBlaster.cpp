//--------------------------------------------------------------------------------------------------
//
//	LOGERR
//
//--------------------------------------------------------------------------------------------------
//
// The MIT License (MIT)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software
// and associated documentation files (the "Software"), to deal in the Software without
// restriction, including without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
// BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
//--------------------------------------------------------------------------------------------------
//
// Copyright (c) 2020 Nic Holthaus
//
//--------------------------------------------------------------------------------------------------
//
// ATTRIBUTION:
//
// ---------------------------------------------------------------------------------------------------------------------
//
/// @brief
/// @details
//
// ---------------------------------------------------------------------------------------------------------------------

//----------------------------
//  INCLUDES
//----------------------------

#include "logBlaster.h"
#include <logerrMacros.h>

#include <concurrent_queue.h>
#include <qCoreAppThread.h>
#include <thread>

#include <QUdpSocket>
#include <QVariant>
#include <utility>

//----------------------------
//  USING DECLARATIONS
//----------------------------

using namespace std::chrono_literals;

//----------------------------------------------------------------------------------------------------------------------
//      CLASS: LogBlasterPrivate
//----------------------------------------------------------------------------------------------------------------------
class LogBlasterPrivate
{
public:
	ENSURE_QAPP

	QHostAddress                  m_host;
	quint16                       m_port = 0;
	std::thread                   m_udpThread;
	concurrent_queue<std::string> m_logQueue;
	std::atomic_bool              m_joinAll = false;
};

//----------------------------------------------------------------------------------------------------------------------
//      FUNCTION: Constructor [public]
//----------------------------------------------------------------------------------------------------------------------
/// @brief Constructor
//----------------------------------------------------------------------------------------------------------------------
LogBlaster::LogBlaster(QHostAddress host, quint16 port)
    : d_ptr(new LogBlasterPrivate)
{
	Q_D(LogBlaster);

	d->m_host      = std::move(host);
	d->m_port      = port;
	d->m_udpThread = std::thread([this]()
	                             {
		                             Q_D(LogBlaster);

		                             QScopedPointer<QUdpSocket> socket(new QUdpSocket);
		                             EXPECTS(socket->bind(QHostAddress(QHostAddress::AnyIPv4), 0));
		                             socket->setSocketOption(QAbstractSocket::MulticastTtlOption, 1);

		                             std::string logMessage;
		                             while (!d->m_joinAll)
		                             {
			                             if (d->m_logQueue.try_pop_for(logMessage, 10ms))
				                             socket->writeDatagram(logMessage.data(), logMessage.size(), d->m_host, d->m_port);
		                             }

		                             // on join, blast whatever is immediately available
		                             while (d->m_logQueue.try_pop_for(logMessage, 0ms))
			                             socket->writeDatagram(logMessage.data(), logMessage.size(), d->m_host, d->m_port);
	                             });
}

//----------------------------------------------------------------------------------------------------------------------
//      FUNCTION: DESTRUCTOR [public]
//----------------------------------------------------------------------------------------------------------------------
/// @brief Destructor
//----------------------------------------------------------------------------------------------------------------------
LogBlaster::~LogBlaster()
{
	Q_D(LogBlaster);
	d->m_joinAll = true;
	if (d->m_udpThread.joinable())
		d->m_udpThread.join();
}

//----------------------------------------------------------------------------------------------------------------------
//      FUNCTION: blast [public]
//----------------------------------------------------------------------------------------------------------------------
/// @brief queues log messages to be multicasted
/// @param str log message
//----------------------------------------------------------------------------------------------------------------------
void LogBlaster::blast(std::string str)
{
	Q_D(LogBlaster);
	d->m_logQueue.emplace(std::move(str));
}