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
	, m_parserThread(THREAD_SETUP({
		ON_DATA_RECEIVED({ EMIT(parse(data)); });
	}))
{
	m_regex.setPatternOptions(QRegularExpression::MultilineOption | QRegularExpression::DotMatchesEverythingOption);
	VERIFY(connect(m_updateTimer, &QTimer::timeout, this, &LogModel::appendRows));
	m_updateTimer->start(250ms);
}

//--------------------------------------------------------------------------------------------------
//	~LogModel () []
//--------------------------------------------------------------------------------------------------
LogModel::~LogModel()
    = default;

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
	else if (parent.model() == this && parent.internalId() == LOG_ENTRY && parent.row() >= 0 &&
	         static_cast<size_t>(parent.row()) < m_logData.size())
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
	if (!child.isValid() || child.model() != this)
		return {};

	if (child.internalId() == LOG_ENTRY)
		return {};
	else
		return index(static_cast<int>(child.internalId()), 0);
}

//--------------------------------------------------------------------------------------------------
//	rowCount () []
//--------------------------------------------------------------------------------------------------
int LogModel::rowCount(const QModelIndex& parent /*= QModelIndex()*/) const
{
	int count{0};

	if (parent.isValid())
		hasChildren(parent) ? count = static_cast<int>(m_logData[static_cast<size_t>(parent.row())].size()) - columnCount(parent) : count = 0;
	else
		count = static_cast<int>(m_logData.size());

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
	else if (parent.model() == this && parent.internalId() == LOG_ENTRY && parent.row() >= 0 &&
	         static_cast<size_t>(parent.row()) < m_logData.size())
		return m_logData[static_cast<size_t>(parent.row())].size() > columnCount(parent);
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

	const int column    = index.column();
	const int row       = index.row();
	const QModelIndex parentIndex = index.parent();
	const int parentRow = parentIndex.row();
	const bool child     = parentIndex.isValid();
	if ((!child && (row < 0 || static_cast<size_t>(row) >= m_logData.size())) ||
	    (child && (parentRow < 0 || static_cast<size_t>(parentRow) >= m_logData.size())))
		return {};
	const QString type = !child ? m_logData[static_cast<size_t>(row)][Column::Type]
	                            : m_logData[static_cast<size_t>(parentRow)][Column::Type];

	QFont boldFont;
	boldFont.setWeight(QFont::Bold);

	switch (role)
	{
		case Qt::DisplayRole:
			if (!child)
				return m_logData[static_cast<size_t>(row)][column];
			else if (column < Column::Message)
				return "";
			else
				return m_logData[static_cast<size_t>(parentRow)][row + columnCount(parentIndex)];
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
				return QBrush("#2db2e7");
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
	if (parent.isValid() || row < 0 || row > rowCount() || count <= 0)
		return false;

	this->beginInsertRows(parent, row, row + count - 1);

	QStringList emptyRow;
	emptyRow.resize(columnCount());
	m_logData.insert(m_logData.begin() + row, static_cast<size_t>(count), emptyRow);

	this->endInsertRows();

	return true;
}

//--------------------------------------------------------------------------------------------------
//	setData (public ) []
//--------------------------------------------------------------------------------------------------
bool LogModel::setData(const QModelIndex& index, const QVariant& value, int role /*= Qt::EditRole*/)
{
	assert(value.metaType().id() == QMetaType::QString);

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
			// Skip empty pieces from the CRLF-terminated detail block so no spurious blank child row appears (see parse()).
			const QStringList details = match.captured(5).split('\n');
			for (const auto& detail : details)
			{
				const QString trimmed = detail.trimmed();
				if (!trimmed.isEmpty())
					m_logData.back().append(trimmed);
			}
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
	m_parserThread.enqueue(std::move(string));
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
		const size_t amountToRemove = m_logData.size() - size;
		m_logData.erase(m_logData.begin(), m_logData.begin() + static_cast<std::ptrdiff_t>(amountToRemove));
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
QStringList LogModel::parse(const std::string& str) const
{
	const QString value = QString::fromStdString(str);

	// don't put whitespace lines into the model
	if (value.trimmed().isEmpty())
		return {};

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
			// Split the detail block into child rows. Skip empty pieces: the block is CRLF-terminated, so splitting on
			// '\n' yields a trailing empty piece (and any blank separator line inside), which would otherwise render as
			// a spurious empty drop-down row between the message and the first trace frame.
			const QStringList details = match.captured(5).split('\n');
			for (const auto& detail : details)
			{
				const QString trimmed = detail.trimmed();
				if (!trimmed.isEmpty())
					parsedList.append(trimmed);
			}
		}
	}

	return parsedList;
}

//--------------------------------------------------------------------------------------------------
//	appendRows (private ) []
//--------------------------------------------------------------------------------------------------
void LogModel::appendRows()
{
	std::deque<QStringList> rows;
	QStringList             row;
	while (m_parserThread.outputs().try_pop(row))
	{
		if (!row.isEmpty())
			rows.push_back(std::move(row));
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
			totalSize -= numToRemove;

			// 3 cases: remove all from the current list, some from both, or all from the new rows
			if (!m_logData.empty())
			{
				auto numToRemoveFromHere = std::min(numToRemove, m_logData.size());
				this->beginRemoveRows(QModelIndex(), 0, static_cast<int>(numToRemoveFromHere - 1));
				m_logData.erase(m_logData.begin(), m_logData.begin() + static_cast<std::ptrdiff_t>(numToRemoveFromHere));
				emit this->endRemoveRows();
				numToRemove -= numToRemoveFromHere;
			}
			if (numToRemove && !rows.empty())
			{
				auto numToRemoveFromHere = std::min(numToRemove, rows.size());
				rows.erase(rows.begin(), rows.begin() + static_cast<std::ptrdiff_t>(numToRemoveFromHere));
				numToRemove -= numToRemoveFromHere;
			}

			assert(numToRemove == 0);
		}

		if (!rows.empty())
		{
			auto first = m_logData.size();
			auto last  = totalSize - 1;
			this->beginInsertRows(QModelIndex(), static_cast<int>(first), static_cast<int>(last));
			m_logData.insert(m_logData.end(), rows.begin(), rows.end());
			this->endInsertRows();
		}
	}
}
