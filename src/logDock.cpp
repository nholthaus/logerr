#include <logDock.h>
#include <LogModel.h>
#include <LogProxyModel.h>
#include <logerr>

#include <QCheckBox>
#include <QFontDatabase>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIntValidator>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QScrollBar>
#include <QStyleFactory>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

//--------------------------------------------------------------------------------------------------
//	LogDock (public ) []
//--------------------------------------------------------------------------------------------------
LogDock::LogDock()
	: QDockWidget("Log Window")
	, m_logModel(new LogModel(this))
	, m_logProxyModel(new LogProxyModel(this))
	, m_logView(new QTreeView(this))
	, m_topLevelWidget(new QFrame(this))
	, m_topLevelLayout(new QVBoxLayout)
	, m_settingsLayout(new QHBoxLayout)
	, m_typesGroupbox(new QGroupBox("Show"))
	, m_settingsGroupBox(new QGroupBox("Settings"))
	, m_errorCheckBox(new QCheckBox("Errors"))
	, m_warningCheckBox(new QCheckBox("Warnings"))
	, m_infoCheckBox(new QCheckBox("Info"))
	, m_debugCheckBox(new QCheckBox("Debug"))
	, m_showTimestampsCheckBox(new QCheckBox("Timestamps"))
	, m_scrollbackLabel(new QLabel("Scrollback Buffer: "))
	, m_scrollbackLineEdit(new QLineEdit)
	, m_autoscrollCheckBox(new QCheckBox("Autoscroll"))
	, m_searchGroupBox(new QGroupBox("Search"))
	, m_searchLineEdit(new QLineEdit)
	, m_matchCaseButton(new QToolButton)
	, m_regexButton(new QToolButton)
{
	QFont monospaceFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);

	this->setWidget(m_topLevelWidget);

	m_topLevelWidget->setLayout(m_topLevelLayout);
	m_topLevelLayout->addLayout(m_settingsLayout);
	m_topLevelLayout->addWidget(m_logView);

	m_settingsLayout->addWidget(m_typesGroupbox);
	m_settingsLayout->addWidget(m_settingsGroupBox);
	m_settingsLayout->setContentsMargins(0,0,0,0);

	m_typesGroupbox->setLayout(new QHBoxLayout);
	m_typesGroupbox->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
	m_typesGroupbox->layout()->addWidget(m_errorCheckBox);
	m_typesGroupbox->layout()->addWidget(m_warningCheckBox);
	m_typesGroupbox->layout()->addWidget(m_infoCheckBox);
	m_typesGroupbox->layout()->addWidget(m_debugCheckBox);
	m_typesGroupbox->layout()->addWidget(m_showTimestampsCheckBox);

	m_errorCheckBox->setChecked(true);
	m_warningCheckBox->setChecked(true);
	m_infoCheckBox->setChecked(true);
	m_debugCheckBox->setChecked(true);
	m_showTimestampsCheckBox->setChecked(true);
	m_autoscrollCheckBox->setChecked(true);

	m_settingsGroupBox->setLayout(new QHBoxLayout);
	m_settingsGroupBox->layout()->addWidget(m_scrollbackLabel);
	m_settingsGroupBox->layout()->addWidget(m_scrollbackLineEdit);
	m_settingsGroupBox->layout()->addWidget(m_autoscrollCheckBox);

	m_scrollbackLineEdit->setValidator(new QIntValidator(0, 1000000, m_scrollbackLineEdit));
	m_scrollbackLineEdit->setText(QString::number(m_logModel->scrollbackBufferSize()));

	m_settingsLayout->addWidget(m_searchGroupBox);
	m_searchGroupBox->setLayout(new QHBoxLayout);
	m_searchGroupBox->layout()->addWidget(m_searchLineEdit);
	m_searchGroupBox->layout()->addWidget(m_matchCaseButton);
	m_searchGroupBox->layout()->addWidget(m_regexButton);

	m_searchLineEdit->setPlaceholderText("Find...");
	m_matchCaseButton->setToolTip("Match Case");
	m_matchCaseButton->setCheckable(true);
	m_matchCaseButton->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
	m_matchCaseButton->setText("Aa");
	m_regexButton->setToolTip("Use Regular Expression");
	m_regexButton->setCheckable(true);
	m_regexButton->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
	m_regexButton->setText(".*");

	m_logProxyModel->setSourceModel(m_logModel);
	m_logProxyModel->setFilterKeyColumn(LogModel::Column::Message);
	m_logProxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);

	m_logView->setModel(m_logProxyModel);
	m_logView->setStyle(QStyleFactory::create("fusion"));
	m_logView->setFont(monospaceFont);
	m_logView->setHeaderHidden(false);
	m_logView->setAllColumnsShowFocus(true);
	m_logView->setUniformRowHeights(true);
	m_logView->setAlternatingRowColors(true);
	m_logView->header()->setSectionResizeMode(QHeaderView::ResizeToContents);

	VERIFY(connect(m_logModel, &QAbstractListModel::rowsInserted, this, &LogDock::autoscroll));
	VERIFY(connect(m_logModel, &QAbstractListModel::rowsRemoved, this, &LogDock::stableScroll));

	VERIFY(connect(m_showTimestampsCheckBox, &QCheckBox::toggled, this, &LogDock::on_showTimestampsCheckBox_toggled));
	VERIFY(connect(m_scrollbackLineEdit, &QLineEdit::textChanged, this, &LogDock::on_scrollbackBufferSize_changed));

	VERIFY(connect(m_errorCheckBox, &QCheckBox::toggled, [this] { m_logProxyModel->setAcceptsErrors(m_errorCheckBox->isChecked()); }));
	VERIFY(connect(m_warningCheckBox, &QCheckBox::toggled, [this] { m_logProxyModel->setAcceptsWarnings(m_warningCheckBox->isChecked()); }));
	VERIFY(connect(m_infoCheckBox, &QCheckBox::toggled, [this] { m_logProxyModel->setAcceptsInfo(m_infoCheckBox->isChecked()); }));
	VERIFY(connect(m_debugCheckBox, &QCheckBox::toggled, [this] { m_logProxyModel->setAcceptsDebug(m_debugCheckBox->isChecked()); }));

	VERIFY(connect(m_searchLineEdit, &QLineEdit::textChanged, this, &LogDock::search));
	VERIFY(connect(m_matchCaseButton, &QToolButton::clicked, [this](bool checked) { checked ? m_logProxyModel->setFilterCaseSensitivity(Qt::CaseSensitive) : m_logProxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive); }));
}

//--------------------------------------------------------------------------------------------------
//	~LogDock (public ) []
//--------------------------------------------------------------------------------------------------
LogDock::~LogDock()
{

}

//--------------------------------------------------------------------------------------------------
//	queueLogEntry (public ) []
//--------------------------------------------------------------------------------------------------
void LogDock::queueLogEntry(std::string str)
{
	m_logModel->queueLogEntry(str);
}

//--------------------------------------------------------------------------------------------------
//	on_scrollbackBufferSize_changed (private ) []
//--------------------------------------------------------------------------------------------------
void LogDock::on_scrollbackBufferSize_changed()
{
	m_logModel->setScrollbackBufferSize(m_scrollbackLineEdit->text().toULongLong());
}

//--------------------------------------------------------------------------------------------------
//	on_showTimestampsCheckBox_toggled (private ) []
//--------------------------------------------------------------------------------------------------
void LogDock::on_showTimestampsCheckBox_toggled()
{
	m_logView->setColumnHidden(0, !m_showTimestampsCheckBox->isChecked());
}

//--------------------------------------------------------------------------------------------------
//	autoscroll (private ) []
//--------------------------------------------------------------------------------------------------
void LogDock::autoscroll()
{
	if (m_autoscrollCheckBox->isChecked())
		m_logView->scrollToBottom();
}

//--------------------------------------------------------------------------------------------------
//	ensureVisible (private ) []
//--------------------------------------------------------------------------------------------------
void LogDock::stableScroll()
{
	if (!m_autoscrollCheckBox->isChecked())
	{
		QScrollBar* verticalScrollBar = m_logView->verticalScrollBar();
		bool bScrolledToTop = verticalScrollBar->value() == verticalScrollBar->minimum();
		int iRowIndex = m_logView->indexAt(QPoint(8, 8)).row();
		int iRowCount = m_logView->model()->rowCount();

		// move scroll bar to keep current items in view (if not scrolled to the top)
		if (!bScrolledToTop)
		{
			iRowCount = m_logView->model()->rowCount() - iRowCount;
			m_logView->scrollTo(m_logView->model()->index(iRowIndex + iRowCount, 0), QAbstractItemView::PositionAtTop);
		}
	}
}

//--------------------------------------------------------------------------------------------------
//	search (private ) []
//--------------------------------------------------------------------------------------------------
void LogDock::search(const QString& value)
{
	if (m_regexButton->isChecked())
		m_logProxyModel->setFilterRegularExpression(value);
	else
		m_logProxyModel->setFilterWildcard(value);
}

