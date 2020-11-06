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
//
// ---------------------------------------------------------------------------------------------------------------------
//
/// @brief      Blasts log messages over a UDP multicast port
/// @details    Uses 239.239.239.239:239
//
// ---------------------------------------------------------------------------------------------------------------------

#ifndef LOGBLASTER_H
#define LOGBLASTER_H

//----------------------------
//  INCLUDES
//----------------------------

#include <QScopedPointer>
#include <QHostAddress>
#include <QUdpSocket>

//----------------------------
//  FORWARD DECLARATIONS
//----------------------------

class LogBlasterPrivate;

//----------------------------------------------------------------------------------------------------------------------
//      CLASS: LogBlaster
//----------------------------------------------------------------------------------------------------------------------
class LogBlaster
{
public:
	LogBlaster(QHostAddress host = QHostAddress(QStringLiteral("239.239.239.239")), quint16 port = 52387);
	~LogBlaster();

	void blast(std::string str);

private:

	Q_DECLARE_PRIVATE(LogBlaster);
	QScopedPointer<LogBlasterPrivate> d_ptr;
};

#endif    // LOGBLASTER_H
