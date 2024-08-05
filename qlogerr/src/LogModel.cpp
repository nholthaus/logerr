#include <LogModel.h>
#include <logerr>
#include <timestampLite.h>

#include <chrono>

#include <QApplication>
#include <QFont>
#include <QRegularExpression>
#include <QStyle>
#include <QTimer>

using namespace std::chrono_literals;

//------------------------------
//	CONSTANTS
//------------------------------

constexpr quintptr    LOG_ENTRY = -1;
constexpr const char* regex     = R"(\s*?\[(.*?)\]\s*?\[(.*?)\]\s*?\[(.*?)\]\s*?(.*?)\n(.*))";

//--------------------------------------------------------------------------------------------------
//	LogModel (public ) []
//--------------------------------------------------------------------------------------------------
LogModel::LogModel(QObject* parent)
    : QAbstractItemModel(parent)
    , m_regex(regex)
    , m_columns(QMetaEnum::fromType<Column>())
    , m_updateTimer(new QTimer(this))
{
	m_regex.setPatternOptions(QRegularExpression::MultilineOption | QRegularExpression::DotMatchesEverythingOption);

	m_parserThread = std::thread([this] { parse(); });

	VERIFY(connect(m_updateTimer, &QTimer::timeout, this, &LogModel::appendRows));
	m_updateTimer->start(250ms);
}

//--------------------------------------------------------------------------------------------------
//	~LogModel () []
//--------------------------------------------------------------------------------------------------
LogModel::~LogModel()
{
	m_joinAll.store(true);
	if (m_parserThread.joinable())
		m_parserThread.join();
}

//--------------------------------------------------------------------------------------------------
//	index () []
//--------------------------------------------------------------------------------------------------
QModelIndex LogModel::index(int row, int column, const QModelIndex& parent /*= QModelIndex()*/) const
{
	if (!this->hasIndex(row, column, parent))
		return {};

	// handle top-level model entries - the most common case
	if (parent == QModelIndex())
	{
		if ((unsigned int) row < m_logData.size() && column < columnCount())
			return createIndex(row, column, LOG_ENTRY);
	}
	else if (parent.isValid())
	{
		const QStringList& logEntry = m_logData[parent.row()];
		if (logEntry.size() >= columnCount(parent))
			return createIndex(row, column, parent.row());
	}

	return {};
}

//--------------------------------------------------------------------------------------------------
//	parent () []
//--------------------------------------------------------------------------------------------------
QModelIndex LogModel::parent(const QModelIndex& child) const
{
	if (child.internalId() == LOG_ENTRY)
		return {};
	else
		return index(child.internalId(), 0);
}

//--------------------------------------------------------------------------------------------------
//	rowCount () []
//--------------------------------------------------------------------------------------------------
int LogModel::rowCount(const QModelIndex& parent /*= QModelIndex()*/) const
{
	int count{0};

	if (parent.isValid())
		hasChildren(parent) ? count = m_logData[parent.row()].size() - columnCount() : count = 0;
	else
		count = m_logData.size();

	return count;
}

//--------------------------------------------------------------------------------------------------
//	columnCount () []
//--------------------------------------------------------------------------------------------------
int LogModel::columnCount(const QModelIndex&) const
{
	return m_columns.keyCount();
}

//--------------------------------------------------------------------------------------------------
//	hasChildren () []
//--------------------------------------------------------------------------------------------------
bool LogModel::hasChildren(const QModelIndex& parent /*= QModelIndex()*/) const
{
	if (!parent.isValid())
		return !m_logData.empty();
	else if (parent.internalId() == LOG_ENTRY)
		return m_logData[parent.row()].size() > columnCount();
	else
		return false;
}

//--------------------------------------------------------------------------------------------------
//	data () []
//--------------------------------------------------------------------------------------------------
QVariant LogModel::data(const QModelIndex& index, int role /*= Qt::DisplayRole*/) const
{
	if (!index.isValid())
		return {};

	int     column    = index.column();
	int     row       = index.row();
	int     parentRow = index.parent().row() - m_numRemoved;
	bool    child     = index.parent().isValid();
	QString type      = !child ? m_logData[row][Column::Type] : m_logData[parentRow][Column::Type];

	QFont boldFont;
	boldFont.setWeight(QFont::Bold);

	switch (role)
	{
		case Qt::DisplayRole:
			if (!child)
				return m_logData[row][column];
			else if (column < Column::Message)
				return "";
			else
				return m_logData[parentRow][row + columnCount()];
			break;
		case Qt::FontRole:
			if (type != "INFO" && column != Column::Timestamp)
				return boldFont;
			return {};
		case Qt::ForegroundRole:
			if (column == Column::Timestamp && type == "INFO")
				return QBrush(Qt::gray);
			if (column == Column::Module && type == "INFO")
				return QBrush(Qt::gray);
			if (column == Column::Type && type == "INFO")
				return QBrush(Qt::gray);
			if (type == "ERROR")
				return QBrush(Qt::red);
			if (type == "WARNING")
				return QBrush("#a67c00");
			if (type == "DEBUG")
				return QBrush("#4E2A84");
			return QBrush(QApplication::palette().color(QPalette::Text));
		default:
			return {};
	}
}

//--------------------------------------------------------------------------------------------------
//	headerData () []
//--------------------------------------------------------------------------------------------------
QVariant LogModel::headerData(int section, Qt::Orientation, int role /*= Qt::DisplayRole*/) const
{
	switch (role)
	{
		case Qt::DisplayRole:
			return m_columns.valueToKey(section);
		default:
			return {};
	}
}

//--------------------------------------------------------------------------------------------------
//	insertRows () []
//--------------------------------------------------------------------------------------------------
bool LogModel::insertRows(int row, int count, const QModelIndex& parent /*= QModelIndex()*/)
{
	assert(row >= 0);
	assert(row <= rowCount());

	this->beginInsertRows(parent, row, row + count);

	auto insertionItr = m_logData.begin();
	std::advance(insertionItr, row);

	for (int i = 0; i < count; ++i)
		m_logData.insert(insertionItr++, QStringList());

	this->endInsertRows();

	return true;
}

//--------------------------------------------------------------------------------------------------
//	setData (public ) []
//--------------------------------------------------------------------------------------------------
bool LogModel::setData(const QModelIndex& index, const QVariant& value, int role /*= Qt::EditRole*/)
{
	assert(value.type() == QVariant::String);

	if (!index.isValid())
		return false;

	m_logData[index.row()][index.column()] = value.toString();

	emit dataChanged(index, index, QVector{role});

	return true;
}

//--------------------------------------------------------------------------------------------------
//	appendRow (public ) [virtual ]
//--------------------------------------------------------------------------------------------------
void LogModel::appendRow(const QString& value)
{
	// don't put whitespace lines into the model
	if (value.trimmed().isEmpty())
		return;

	this->beginInsertRows(QModelIndex(), rowCount(), rowCount());

	auto match = m_regex.match(value);

	if (!match.hasMatch())
	{
		// this can happen for raw cout writes that didn't use the macros.
		QStringList valueList = value.split('\n');
		m_logData.emplace_back(QString::fromStdString(TimestampLite()));
		m_logData.back().append("unset_name");
		m_logData.back().append("INFO");
		m_logData.back().append(valueList.front().trimmed());
		valueList.pop_front();
		if (!valueList.isEmpty())
			m_logData.back().append(valueList);
	}
	else
	{
		m_logData.emplace_back(match.captured(1));
		m_logData.back().append(match.captured(2));
		m_logData.back().append(match.captured(3));
		m_logData.back().append(match.captured(4).trimmed());
		if (!match.captured(5).isEmpty())
		{
			QStringList details = match.captured(5).split('\n');
			for (auto& detail : details)
				m_logData.back().append(detail.trimmed());
		}
	}

	this->endInsertRows();
}

//--------------------------------------------------------------------------------------------------
//	appendRow (public ) [virtual ]
//--------------------------------------------------------------------------------------------------
void LogModel::appendRow(const std::string& value)
{
	return appendRow(QString::fromStdString(value));
}

//--------------------------------------------------------------------------------------------------
//	write (public ) []
//--------------------------------------------------------------------------------------------------
void LogModel::queueLogEntry(std::string string)
{
	m_inbox.push(std::move(string));
}

//--------------------------------------------------------------------------------------------------
//	scrollbackBufferSize (public ) []
//--------------------------------------------------------------------------------------------------
size_t LogModel::scrollbackBufferSize() const noexcept
{
	return m_scrollbackBufferSize;
}

//--------------------------------------------------------------------------------------------------
//	setScrollbackBufferSize (public ) []
//--------------------------------------------------------------------------------------------------
void LogModel::setScrollbackBufferSize(size_t size)
{
	if (size < m_logData.size())
	{
		emit this->beginResetModel();
		size_t amountToRemove = m_logData.size() - size;
		m_logData.erase(m_logData.begin(), m_logData.begin() + amountToRemove);
		m_scrollbackBufferSize = size;
		emit this->endResetModel();
	}
	else
	{
		m_scrollbackBufferSize = size;
	}
}

//--------------------------------------------------------------------------------------------------
//	parse (private ) []
//--------------------------------------------------------------------------------------------------
void LogModel::parse()
{
	std::string str;
	while (!m_joinAll)
	{
		if (m_inbox.try_pop_for(str, 100ms))
		{
			QString value = QString::fromStdString(str);

			// don't put whitespace lines into the model
			if (value.trimmed().isEmpty())
				continue;

			auto match = m_regex.match(value);

			QStringList parsedList;

			if (!match.hasMatch())
			{
				// this can happen for raw cout writes that didn't use the macros.
				QStringList valueList = value.split('\n');
				parsedList.append(QString::fromStdString(TimestampLite()));
				parsedList.append("unset_name");
				parsedList.append("INFO");
				parsedList.append(valueList.front().trimmed());
				valueList.pop_front();
				if (!valueList.isEmpty())
					parsedList.append(valueList);
			}
			else
			{
				parsedList.append(match.captured(1));
				parsedList.append(match.captured(2));
				parsedList.append(match.captured(3));
				parsedList.append(match.captured(4).trimmed());
				if (!match.captured(5).isEmpty())
				{
					QStringList details = match.captured(5).split('\n');
					for (auto& detail : details)
						parsedList.append(detail.trimmed());
				}
			}

			m_outbox.emplace(std::move(parsedList));
		}
	}
}

//--------------------------------------------------------------------------------------------------
//	appendRows (private ) []
//--------------------------------------------------------------------------------------------------
void LogModel::appendRows()
{
	QStringList             row;
	std::deque<QStringList> rows;
	while (m_outbox.try_pop(row) && !m_joinAll)
	{
		rows.push_back(row);
	}

	if (!rows.empty())
	{
		// check if we exceed the scroll buffer, and remove stuff if so.
		// ensure there are always at least scroll buffer size entries. Otherwise once the buffer fills up the list view
		// will appear to the user to be 'auto-scrolling' all the time
		auto totalSize = m_logData.size() + rows.size();
		if (totalSize > 2 * m_scrollbackBufferSize)
		{
			auto numToRemove = totalSize - scrollbackBufferSize();
			m_numRemoved += numToRemove;
			totalSize -= numToRemove;

			// 3 cases: remove all from the current list, some from both, or all from the new rows
			if (!m_logData.empty())
			{
				auto numToRemoveFromHere = std::min(numToRemove, m_logData.size());
				this->beginRemoveRows(QModelIndex(), 0, numToRemoveFromHere - 1);
				m_logData.erase(m_logData.begin(), m_logData.begin() + numToRemoveFromHere);
				emit this->endRemoveRows();
				numToRemove -= numToRemoveFromHere;
			}
			if (numToRemove && !rows.empty())
			{
				auto numToRemoveFromHere = std::min(numToRemove, rows.size());
				rows.erase(rows.begin(), rows.begin() + numToRemoveFromHere);
				numToRemove -= numToRemoveFromHere;
			}

			assert(numToRemove == 0);
		}

		auto first = m_logData.size();
		auto last  = totalSize - 1;
		this->beginInsertRows(QModelIndex(), first, last);
		m_logData.insert(m_logData.end(), rows.begin(), rows.end());
		this->endInsertRows();
	}
}