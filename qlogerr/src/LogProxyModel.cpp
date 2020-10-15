#include <LogProxyModel.h>
#include <LogModel.h>

//--------------------------------------------------------------------------------------------------
//	LogProxyModel (public ) []
//--------------------------------------------------------------------------------------------------
LogProxyModel::LogProxyModel(QObject* parent /*= nullptr*/)
	: QSortFilterProxyModel(parent)
{

}

LogProxyModel::~LogProxyModel() = default;

//--------------------------------------------------------------------------------------------------
//	acceptsErrors (public ) []
//--------------------------------------------------------------------------------------------------
bool LogProxyModel::acceptsErrors() const
{
	return m_acceptsErrors;
}

//--------------------------------------------------------------------------------------------------
//	setAcceptsErrors (public ) []
//--------------------------------------------------------------------------------------------------
void LogProxyModel::setAcceptsErrors(bool val)
{
	m_acceptsErrors = val;
	invalidateFilter();
}

//--------------------------------------------------------------------------------------------------
//	acceptsWarnings (public ) []
//--------------------------------------------------------------------------------------------------
bool LogProxyModel::acceptsWarnings() const
{
	return m_acceptsWarnings;
}

//--------------------------------------------------------------------------------------------------
//	setAcceptsWarnings (public ) []
//--------------------------------------------------------------------------------------------------
void LogProxyModel::setAcceptsWarnings(bool val)
{
	m_acceptsWarnings = val;
	invalidateFilter();
}

//--------------------------------------------------------------------------------------------------
//	acceptsInfo (public ) []
//--------------------------------------------------------------------------------------------------
bool LogProxyModel::acceptsInfo() const
{
	return m_acceptsInfo;
}

//--------------------------------------------------------------------------------------------------
//	setAcceptsInfo (public ) []
//--------------------------------------------------------------------------------------------------
void LogProxyModel::setAcceptsInfo(bool val)
{
	m_acceptsInfo = val;
	invalidateFilter();
}

//--------------------------------------------------------------------------------------------------
//	acceptsDebug (public ) []
//--------------------------------------------------------------------------------------------------
bool LogProxyModel::acceptsDebug() const
{
	return m_acceptsDebug;
}

//--------------------------------------------------------------------------------------------------
//	setAcceptsDebug (public ) []
//--------------------------------------------------------------------------------------------------
void LogProxyModel::setAcceptsDebug(bool val)
{
	m_acceptsDebug = val;
	invalidateFilter();
}

//--------------------------------------------------------------------------------------------------
//	acceptsTimestamps (public ) []
//--------------------------------------------------------------------------------------------------
bool LogProxyModel::acceptsTimestamps() const
{
	return m_acceptsTimestamps;
}

//--------------------------------------------------------------------------------------------------
//	setAcceptsTimestamps (public ) []
//--------------------------------------------------------------------------------------------------
void LogProxyModel::setAcceptsTimestamps(bool val)
{
	m_acceptsTimestamps = val;
	invalidateFilter();
}

//--------------------------------------------------------------------------------------------------
//	filterAcceptsRow (public ) []
//--------------------------------------------------------------------------------------------------
bool LogProxyModel::filterAcceptsRow(int source_row, const QModelIndex& source_parent) const
{
	QModelIndex source_type_index = sourceModel()->index(source_row, LogModel::Column::Type, source_parent);
	QModelIndex source_msg_index = sourceModel()->index(source_row, LogModel::Column::Message, source_parent);

	QString source_type = sourceModel()->data(source_type_index).toString();
	QString source_message = sourceModel()->data(source_msg_index).toString();

	if (source_type == "ERROR" && !m_acceptsErrors)
		return false;
	if (source_type == "WARNING" && !m_acceptsWarnings)
		return false;
	if (source_type == "INFO" && !m_acceptsInfo)
		return false;
	if (source_type == "DEBUG" && !m_acceptsDebug)
		return false;

	return QSortFilterProxyModel::filterAcceptsRow(source_row, source_parent);
}
