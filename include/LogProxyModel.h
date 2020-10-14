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
/// @file	LogProxyModel.h
/// @brief	
//
//--------------------------------------------------------------------------------------------------

#pragma once
#ifndef LogProxyModel_h__
#define LogProxyModel_h__

//-------------------------
//	INCLUDES
//-------------------------

#include <QSortFilterProxyModel> 

//-------------------------
//	FORWARD DECLARATIONS
//-------------------------

class QRegularExpression;

//--------------------------------------------------------------------------------------------------
//	LogProxyModel
//--------------------------------------------------------------------------------------------------

class LogProxyModel : public QSortFilterProxyModel
{
public:

	LogProxyModel(QObject* parent = nullptr);
	virtual ~LogProxyModel();
	
	
	bool acceptsErrors() const;
	bool acceptsWarnings() const;
	bool acceptsInfo() const;
	bool acceptsDebug() const;
	bool acceptsTimestamps() const;

	void setAcceptsErrors(bool val);
	void setAcceptsWarnings(bool val);
	void setAcceptsInfo(bool val);
	void setAcceptsDebug(bool val);
	void setAcceptsTimestamps(bool val);

protected:

	virtual bool filterAcceptsRow(int source_row, const QModelIndex& source_parent) const override;

private:

	bool m_acceptsErrors = true;
	bool m_acceptsWarnings = true;
	bool m_acceptsInfo = true;
	bool m_acceptsDebug = true;
	bool m_acceptsTimestamps = true;
	QRegularExpression m_regex;
};

#endif // LogProxyModel_h__
