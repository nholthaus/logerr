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
#include "logChannel.h"
#include "QEventThread.h"
#include <logerrMacros.h>

#include <qCoreAppThread.h>

#include <future>

#include <QUdpSocket>
#include <QVariant>
#include <utility>

//----------------------------
//  USING DECLARATIONS
//----------------------------

//----------------------------------------------------------------------------------------------------------------------
//      CLASS: LogBlasterPrivate
//----------------------------------------------------------------------------------------------------------------------
class LogBlasterPrivate
{
public:
	LogBlasterPrivate(QHostAddress host, quint16 port)
	    : m_host(std::move(host))
	    , m_port(port)
	    , m_udpThread(THREAD_SETUP({
		    auto* socket = new QUdpSocket(eventLoop);
		    EXPECTS(socket->bind(QHostAddress(QHostAddress::AnyIPv4), 0));
		    socket->setSocketOption(QAbstractSocket::MulticastTtlOption, 1);
		    input.onDataReceived([this, socket](const std::string& data) {
			    socket->writeDatagram(data.data(), static_cast<qint64>(data.size()), m_host, m_port);
		    });
	    }))
	{
	}

	void flush()
	{
		// runOnThread shares the input FIFO, so reaching this promise proves that every blast accepted before it was sent.
		auto complete = std::make_shared<std::promise<void>>();
		auto finished = complete->get_future();
		m_udpThread.runOnThread([complete] { complete->set_value(); });
		finished.wait();
	}

	void blast(std::string str)
	{
		m_udpThread.enqueue(std::move(str));
	}

private:
	ENSURE_QAPP

	QHostAddress m_host;
	quint16      m_port = 0;

	// Declared last: its destructor joins before the state used by worker callbacks is destroyed.
	QEventThread<std::string> m_udpThread;
};

//----------------------------------------------------------------------------------------------------------------------
//      FUNCTION: Constructor [public]
//----------------------------------------------------------------------------------------------------------------------
/// @brief Constructor
//----------------------------------------------------------------------------------------------------------------------
LogBlaster::LogBlaster(QHostAddress host, quint16 port)
    : d_ptr(new LogBlasterPrivate(host.isNull() ? LogChannel::group() : std::move(host),
	                              port == 0 ? LogChannel::port() : port))
{
}

//----------------------------------------------------------------------------------------------------------------------
//      FUNCTION: DESTRUCTOR [public]
//----------------------------------------------------------------------------------------------------------------------
/// @brief Destructor
//----------------------------------------------------------------------------------------------------------------------
LogBlaster::~LogBlaster()
{
	Q_D(LogBlaster);
	d->flush();
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
	d->blast(std::move(str));
}
