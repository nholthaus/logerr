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
	setFilterFlag(m_acceptsErrors, val);
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
	setFilterFlag(m_acceptsWarnings, val);
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
	setFilterFlag(m_acceptsInfo, val);
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
	setFilterFlag(m_acceptsDebug, val);
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
	setFilterFlag(m_acceptsTimestamps, val);
}

//--------------------------------------------------------------------------------------------------
//	acceptsModules (public ) []
//--------------------------------------------------------------------------------------------------
bool LogProxyModel::acceptsModules() const
{
	return m_acceptsModules;
}

//--------------------------------------------------------------------------------------------------
//	setAcceptsModules (public ) []
//--------------------------------------------------------------------------------------------------
void LogProxyModel::setAcceptsModules(bool val)
{
	setFilterFlag(m_acceptsModules, val);
}

//--------------------------------------------------------------------------------------------------
//	setFilterFlag (private ) []
//--------------------------------------------------------------------------------------------------
void LogProxyModel::setFilterFlag(bool& flag, bool value)
{
	if (flag == value)
		return;

#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
	beginFilterChange();
	flag = value;
	endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
	flag = value;
	invalidateFilter();
#endif
}

//--------------------------------------------------------------------------------------------------
//	filterAcceptsRow (public ) []
//--------------------------------------------------------------------------------------------------
bool LogProxyModel::filterAcceptsRow(int source_row, const QModelIndex& source_parent) const
{
	const QModelIndex source_type_index = sourceModel()->index(source_row, LogModel::Column::Type, source_parent);
	const QString source_type = sourceModel()->data(source_type_index).toString();

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

//----------------------------------------------------------------------------------------------------------------------
//      FUNCTION: less than []
//----------------------------------------------------------------------------------------------------------------------
bool LogProxyModel::lessThan(const QModelIndex& source_left, const QModelIndex& source_right) const
{
	return sourceModel()->data(source_left).toString() < sourceModel()->data(source_right).toString();
}
