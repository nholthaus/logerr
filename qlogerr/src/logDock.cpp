#include <LogModel.h>
#include <LogProxyModel.h>
#include <logDock.h>
#include <logerr>

#include <QCheckBox>
#include <QFontDatabase>
#include <QGroupBox>
#include <QHeaderView>
#include <QIntValidator>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QScrollBar>
#include <QSettings>
#include <QStyle>
#include <QStyleFactory>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>
#include <utility>

namespace
{
	// The QSettings group every LogDock preference lives under. Keeping every key beneath one group keeps the log
	// dock's persisted state self-contained and easy to clear.
	constexpr const char* SETTINGS_GROUP = "logerr/logDock";
}    // namespace

Q_DECLARE_METATYPE(std::string)

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
    , m_searchGroupBox(new QGroupBox("Search"))
    , m_errorCheckBox(new QCheckBox("Errors"))
    , m_warningCheckBox(new QCheckBox("Warnings"))
    , m_infoCheckBox(new QCheckBox("Info"))
    , m_debugCheckBox(new QCheckBox("Debug"))
    , m_showTimestampsCheckBox(new QCheckBox("Timestamps"))
    , m_showModulesCheckBox(new QCheckBox("Modules"))
    , m_scrollbackLabel(new QLabel("Scrollback Buffer: "))
    , m_scrollbackLineEdit(new QLineEdit)
    , m_autoscrollCheckBox(new QCheckBox("Autoscroll"))
    , m_searchLineEdit(new QLineEdit)
    , m_matchCaseButton(new QToolButton)
    , m_regexButton(new QToolButton)
{
	qRegisterMetaType<std::string>();

	const QFont monospaceFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);

	this->setWidget(m_topLevelWidget);

	m_topLevelWidget->setLayout(m_topLevelLayout);
	m_topLevelLayout->addLayout(m_settingsLayout);
	m_topLevelLayout->addWidget(m_logView);

	m_settingsLayout->addWidget(m_typesGroupbox);
	m_settingsLayout->addWidget(m_settingsGroupBox);
	m_settingsLayout->setContentsMargins(0, 0, 0, 0);

	m_typesGroupbox->setLayout(new QHBoxLayout);
	m_typesGroupbox->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
	m_typesGroupbox->layout()->addWidget(m_errorCheckBox);
	m_typesGroupbox->layout()->addWidget(m_warningCheckBox);
	m_typesGroupbox->layout()->addWidget(m_infoCheckBox);
	m_typesGroupbox->layout()->addWidget(m_debugCheckBox);
	m_typesGroupbox->layout()->addWidget(m_showTimestampsCheckBox);
	m_typesGroupbox->layout()->addWidget(m_showModulesCheckBox);

	m_errorCheckBox->setChecked(true);
	m_warningCheckBox->setChecked(true);
	m_infoCheckBox->setChecked(true);
	m_debugCheckBox->setChecked(true);
	m_showTimestampsCheckBox->setChecked(true);
	m_showModulesCheckBox->setChecked(true);
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
	m_logProxyModel->setDynamicSortFilter(true);

	m_logView->setModel(m_logProxyModel);
	if (auto* fusionStyle = QStyleFactory::create("fusion"))
	{
		fusionStyle->setParent(m_logView);
		m_logView->setStyle(fusionStyle);
	}
	m_logView->setFont(monospaceFont);
	m_logView->setHeaderHidden(false);
	m_logView->setAllColumnsShowFocus(true);
	m_logView->setUniformRowHeights(true);
	m_logView->setAlternatingRowColors(true);
	m_logView->setSortingEnabled(true);
	m_logView->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
	m_logView->header()->setSortIndicator(0, Qt::AscendingOrder);

	VERIFY(connect(m_logModel, &QAbstractListModel::rowsInserted, this, &LogDock::autoscroll));
	VERIFY(connect(m_logModel, &QAbstractListModel::rowsRemoved, this, &LogDock::stableScroll));

	VERIFY(connect(m_showTimestampsCheckBox, &QCheckBox::toggled, this, &LogDock::on_showTimestampsCheckBox_toggled));
	VERIFY(connect(m_showModulesCheckBox, &QCheckBox::toggled, this, &LogDock::on_showModulesCheckBox_toggled));
	VERIFY(connect(m_scrollbackLineEdit, &QLineEdit::textChanged, this, &LogDock::on_scrollbackBufferSize_changed));

	VERIFY(connect(m_errorCheckBox, &QCheckBox::toggled, [this]
	               { m_logProxyModel->setAcceptsErrors(m_errorCheckBox->isChecked()); }));
	VERIFY(connect(m_warningCheckBox, &QCheckBox::toggled, [this]
	               { m_logProxyModel->setAcceptsWarnings(m_warningCheckBox->isChecked()); }));
	VERIFY(connect(m_infoCheckBox, &QCheckBox::toggled, [this]
	               { m_logProxyModel->setAcceptsInfo(m_infoCheckBox->isChecked()); }));
	VERIFY(connect(m_debugCheckBox, &QCheckBox::toggled, [this]
	               { m_logProxyModel->setAcceptsDebug(m_debugCheckBox->isChecked()); }));

	VERIFY(connect(m_searchLineEdit, &QLineEdit::textChanged, this, &LogDock::search));
	VERIFY(connect(m_matchCaseButton, &QToolButton::clicked, [this](bool checked)
	               { checked ? m_logProxyModel->setFilterCaseSensitivity(Qt::CaseSensitive) : m_logProxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive); }));

	// Persist every user-facing preference (the level filters, the timestamp/module column toggles, autoscroll, the
	// match-case/regex search modes, and the scrollback size) so the dock reopens the way the operator left it. Each
	// control writes the whole set on any change; restoreSettings() below reapplies the saved set once at construction,
	// AFTER the defaults are set, so a saved value wins over the default and drives the connected slot via toggled().
	for (QCheckBox* box : {m_errorCheckBox, m_warningCheckBox, m_infoCheckBox, m_debugCheckBox, m_showTimestampsCheckBox,
	                       m_showModulesCheckBox, m_autoscrollCheckBox})
		VERIFY(connect(box, &QCheckBox::toggled, this, &LogDock::saveSettings));
	for (QToolButton* button : {m_matchCaseButton, m_regexButton})
		VERIFY(connect(button, &QToolButton::toggled, this, &LogDock::saveSettings));
	VERIFY(connect(m_scrollbackLineEdit, &QLineEdit::textChanged, this, &LogDock::saveSettings));

	restoreSettings();
}

//--------------------------------------------------------------------------------------------------
//	saveSettings (private ) []
//--------------------------------------------------------------------------------------------------
void LogDock::saveSettings() const
{
	// While restoreSettings() is applying saved values, each setChecked/setText fires toggled/textChanged. Without this
	// guard, the FIRST restored control that changes value would trigger a full save that captures every OTHER control
	// still at its constructor default - overwriting their saved values with defaults, so only one preference survives.
	// The filter/column slots (connected separately) still run during restore; only the persistence write is gated.
	if (m_restoring)
		return;

	QSettings settings;
	settings.beginGroup(SETTINGS_GROUP);
	settings.setValue("showErrors", m_errorCheckBox->isChecked());
	settings.setValue("showWarnings", m_warningCheckBox->isChecked());
	settings.setValue("showInfo", m_infoCheckBox->isChecked());
	settings.setValue("showDebug", m_debugCheckBox->isChecked());
	settings.setValue("showTimestamps", m_showTimestampsCheckBox->isChecked());
	settings.setValue("showModules", m_showModulesCheckBox->isChecked());
	settings.setValue("autoscroll", m_autoscrollCheckBox->isChecked());
	settings.setValue("matchCase", m_matchCaseButton->isChecked());
	settings.setValue("regex", m_regexButton->isChecked());
	settings.setValue("scrollback", m_scrollbackLineEdit->text());
	settings.endGroup();
}

//--------------------------------------------------------------------------------------------------
//	restoreSettings (private ) []
//--------------------------------------------------------------------------------------------------
void LogDock::restoreSettings() const
{
	// Suppress the persistence write while applying values (see saveSettings): the filter/column slots still fire, but
	// the whole set is not re-saved control-by-control mid-restore. Reset on every exit path.
	m_restoring = true;
	struct RestoreGuard
	{
		bool& flag;
		~RestoreGuard() { flag = false; }
	} guard{m_restoring};

	QSettings settings;
	settings.beginGroup(SETTINGS_GROUP);
	// Reapply each saved value, defaulting to the just-set-in-constructor default when no preference was ever stored.
	// setChecked/setText fire toggled/textChanged, so the connected slot (the proxy-model filter, the column-hide, the
	// scrollback resize) applies the restored value with no extra wiring.
	m_errorCheckBox->setChecked(settings.value("showErrors", m_errorCheckBox->isChecked()).toBool());
	m_warningCheckBox->setChecked(settings.value("showWarnings", m_warningCheckBox->isChecked()).toBool());
	m_infoCheckBox->setChecked(settings.value("showInfo", m_infoCheckBox->isChecked()).toBool());
	m_debugCheckBox->setChecked(settings.value("showDebug", m_debugCheckBox->isChecked()).toBool());
	m_showTimestampsCheckBox->setChecked(settings.value("showTimestamps", m_showTimestampsCheckBox->isChecked()).toBool());
	m_showModulesCheckBox->setChecked(settings.value("showModules", m_showModulesCheckBox->isChecked()).toBool());
	m_autoscrollCheckBox->setChecked(settings.value("autoscroll", m_autoscrollCheckBox->isChecked()).toBool());
	m_matchCaseButton->setChecked(settings.value("matchCase", m_matchCaseButton->isChecked()).toBool());
	m_regexButton->setChecked(settings.value("regex", m_regexButton->isChecked()).toBool());
	const QString scrollback = settings.value("scrollback", m_scrollbackLineEdit->text()).toString();
	if (!scrollback.isEmpty())
		m_scrollbackLineEdit->setText(scrollback);
	settings.endGroup();
}

//--------------------------------------------------------------------------------------------------
//	~LogDock (public ) []
//--------------------------------------------------------------------------------------------------
LogDock::~LogDock() = default;

//--------------------------------------------------------------------------------------------------
//	write (public ) []
//--------------------------------------------------------------------------------------------------
void LogDock::queueLogEntry(std::string str) const
{
	m_logModel->queueLogEntry(std::move(str));
}

//--------------------------------------------------------------------------------------------------
//	on_scrollbackBufferSize_changed (private ) []
//--------------------------------------------------------------------------------------------------
void LogDock::on_scrollbackBufferSize_changed() const
{
	m_logModel->setScrollbackBufferSize(m_scrollbackLineEdit->text().toULongLong());
}

//--------------------------------------------------------------------------------------------------
//	on_showTimestampsCheckBox_toggled (private ) []
//--------------------------------------------------------------------------------------------------
void LogDock::on_showTimestampsCheckBox_toggled() const
{
	m_logView->setColumnHidden(LogModel::Column::Timestamp, !m_showTimestampsCheckBox->isChecked());
}

//--------------------------------------------------------------------------------------------------
//	on_showModulesCheckBox_toggled (private ) []
//--------------------------------------------------------------------------------------------------
void LogDock::on_showModulesCheckBox_toggled() const
{
	m_logView->setColumnHidden(LogModel::Column::Module, !m_showModulesCheckBox->isChecked());
}

//--------------------------------------------------------------------------------------------------
//	autoscroll (private ) []
//--------------------------------------------------------------------------------------------------
void LogDock::autoscroll() const
{
	if (m_autoscrollCheckBox->isChecked())
		m_logView->scrollToBottom();
}

//--------------------------------------------------------------------------------------------------
//	ensureVisible (private ) []
//--------------------------------------------------------------------------------------------------
void LogDock::stableScroll() const
{
	if (!m_autoscrollCheckBox->isChecked())
	{
		const QScrollBar* verticalScrollBar = m_logView->verticalScrollBar();
		const bool        bScrolledToTop    = verticalScrollBar->value() == verticalScrollBar->minimum();
		const int         iRowIndex         = m_logView->indexAt(QPoint(8, 8)).row();
		int         iRowCount         = m_logView->model()->rowCount();

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
void LogDock::search(const QString& value) const
{
	if (m_regexButton->isChecked())
		m_logProxyModel->setFilterRegularExpression(value);
	else
		m_logProxyModel->setFilterWildcard(value);
}
