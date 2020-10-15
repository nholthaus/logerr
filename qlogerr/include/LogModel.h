//--------------------------------------------------------------------------------------------------
// 
//	
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
//--------------------------------------------------------------------------------------------------
//
/// @file	LogModel.h
/// @brief	
//
//--------------------------------------------------------------------------------------------------

#pragma once
#ifndef LogModel_h__
#define LogModel_h__

//-------------------------
//	INCLUDES
//-------------------------

#include <concurrent_queue.h>

#include <deque>
#include <string>
#include <thread>

#include <QAbstractItemModel>
#include <QMetaEnum>
#include <QRegularExpression>
#include <QStringList>

//-------------------------
//	FORWARD DECLARATIONS
//-------------------------

class QModelIndex;
class QTimer;
class QVariant;


//--------------------------------------------------------------------------------------------------
//	LogModel
//--------------------------------------------------------------------------------------------------

class LogModel : public QAbstractItemModel
{
	Q_GADGET

public:

	enum Column
	{
		Timestamp = 0,
		Type = 1,
		Message = 2,
	};
	Q_ENUM(Column);

public:

	LogModel(QObject* parent = nullptr);
	virtual ~LogModel();
	
	virtual QModelIndex	index(int row, int column, const QModelIndex& parent = QModelIndex()) const override;
	virtual QModelIndex parent(const QModelIndex& child) const override;
	virtual int	rowCount(const QModelIndex& parent = QModelIndex()) const override;
	virtual int columnCount(const QModelIndex& parent = QModelIndex()) const override;
	virtual bool hasChildren(const QModelIndex& parent = QModelIndex()) const override;
	virtual QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
	virtual QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
	virtual bool insertRows(int row, int count, const QModelIndex& parent = QModelIndex()) override;
	virtual bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;
	
	virtual void appendRow(const QString& value);
	virtual void appendRow(const std::string& value);

public slots:

	void queueLogEntry(std::string string);
	size_t scrollbackBufferSize() const noexcept;
	void setScrollbackBufferSize(size_t size);

private:

	void parse();
	void appendRows();

protected:

	QRegularExpression				m_regex;
	std::deque<QStringList>			m_logData;
	QMetaEnum						m_columns;

	concurrent_queue<std::string>	m_inbox;
	concurrent_queue<QStringList>	m_outbox;
	QTimer*							m_updateTimer;
	std::thread						m_parserThread;

	std::atomic_bool				m_joinAll = false;

	size_t							m_scrollbackBufferSize = 10000;
	size_t							m_numRemoved = 0;			// The number of entries removed from the model for exceeding the scroll buffer size


};

#endif // LogModel_h__
