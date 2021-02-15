//------------------------------
//	INCLUDES
//------------------------------

#include <ExceptionDialog.h>
#include <logerr>
#include <qappinfo.h>

#include <QApplication>
#include <QClipboard>
#include <QDebug>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollBar>
#include <QStyle>
#include <QVBoxLayout>

//--------------------------------------------------------------------------------------------------
//	CONSTRUCTOR
//--------------------------------------------------------------------------------------------------
ExceptionDialog::ExceptionDialog(const StackTraceException& exception, bool fatal, QWidget* parent /*= nullptr*/)
    : QDialog(parent)
    , m_fatal(fatal)
    , m_errorMessage(QString::fromStdString(exception.errorMessage()))
    , m_errorDetails(QString::fromStdString(exception.errorDetails()))
    , m_filename(QString::fromStdString(exception.filename()))
    , m_function(QString::fromStdString(exception.function()))
    , m_line(QString::number(exception.line()))
    , m_errorIcon(new QLabel(this))
    , m_errorMessageLabel(new QLabel(m_errorMessage.prepend("ERROR: "), this))
    , m_errorLocationLabel(new QLabel(QString("in: %1 at: %2:%3").arg(m_function).arg(m_filename).arg(m_line), this))
    , m_detailsGroupBox(new QGroupBox())
    , m_detailsTextBrowser(new CorrectlySizedTextBrowser(this))
    , m_applicationInfoButton(new QPushButton("App Info"))
    , m_versionInfoButton(new QPushButton("Version Info"))
    , m_buildInfoButton(new QPushButton("Build Info"))
    , m_hostInfoButton(new QPushButton("Host Info"))
    , m_StackTraceButton(new QPushButton("Stack Trace"))
    , m_showDetailsButton(new QPushButton("Show Details", this))
    , m_copyButton(new QPushButton("Copy Error", this))
    , m_okButton(new QPushButton("OK", this))
    , m_topLayout(new QVBoxLayout)
    , m_errorLayout(new QHBoxLayout)
    , m_detailsGroupBoxLayout(new QVBoxLayout)
    , m_detailsButtonLayout(new QHBoxLayout)
    , m_buttonLayout(new QHBoxLayout)
{
	setupUI();
}

//--------------------------------------------------------------------------------------------------
//	ExceptionDialog (public ) []
//--------------------------------------------------------------------------------------------------
ExceptionDialog::ExceptionDialog(const std::exception& exception, bool fatal /*= false*/, QWidget* parent /*= nullptr*/)
    : QDialog(parent)
    , m_fatal(fatal)
    , m_errorMessage(QString("An unrecoverable error has occurred.\n").append(QAPPINFO::name()).append(" must now exit."))
    , m_errorDetails(exception.what())
    , m_filename("")
    , m_function("")
    , m_line("")
    , m_errorIcon(new QLabel(this))
    , m_errorMessageLabel(new QLabel(m_errorMessage.prepend("ERROR: "), this))
    , m_errorLocationLabel(new QLabel(QString("in: %1 at: %2:%3").arg(m_function).arg(m_filename).arg(m_line), this))
    , m_detailsGroupBox(new QGroupBox())
    , m_detailsTextBrowser(new CorrectlySizedTextBrowser(this))
    , m_applicationInfoButton(new QPushButton("App Info"))
    , m_versionInfoButton(new QPushButton("Version Info"))
    , m_buildInfoButton(new QPushButton("Build Info"))
    , m_hostInfoButton(new QPushButton("Host Info"))
    , m_StackTraceButton(new QPushButton("Stack Trace"))
    , m_showDetailsButton(new QPushButton("Show Details", this))
    , m_copyButton(new QPushButton("Copy Error", this))
    , m_okButton(new QPushButton("OK", this))
    , m_topLayout(new QVBoxLayout)
    , m_errorLayout(new QHBoxLayout)
    , m_detailsGroupBoxLayout(new QVBoxLayout)
    , m_detailsButtonLayout(new QHBoxLayout)
    , m_buttonLayout(new QHBoxLayout)
{
	setupUI();
	m_errorLocationLabel->hide();

	m_applicationInfoButton->hide();
	m_versionInfoButton->hide();
	m_buildInfoButton->hide();
	m_hostInfoButton->hide();
	m_StackTraceButton->hide();
}

//--------------------------------------------------------------------------------------------------
//	ExceptionDialog (public ) []
//--------------------------------------------------------------------------------------------------
ExceptionDialog::ExceptionDialog(const char* msg, bool fatal /*= false*/, QWidget* parent /*= nullptr*/)
    : QDialog(parent)
    , m_fatal(fatal)
    , m_errorMessage(QString("An unrecoverable error has occurred.\n").append(QAPPINFO::name()).append(" must now exit."))
    , m_errorDetails(msg)
    , m_filename("")
    , m_function("")
    , m_line("")
    , m_errorIcon(new QLabel(this))
    , m_errorMessageLabel(new QLabel(m_errorMessage.prepend("ERROR: "), this))
    , m_errorLocationLabel(new QLabel(QString("in: %1 at: %2:%3").arg(m_function).arg(m_filename).arg(m_line), this))
    , m_detailsGroupBox(new QGroupBox())
    , m_detailsTextBrowser(new CorrectlySizedTextBrowser(this))
    , m_applicationInfoButton(new QPushButton("App Info"))
    , m_versionInfoButton(new QPushButton("Version Info"))
    , m_buildInfoButton(new QPushButton("Build Info"))
    , m_hostInfoButton(new QPushButton("Host Info"))
    , m_StackTraceButton(new QPushButton("Stack Trace"))
    , m_showDetailsButton(new QPushButton("Show Details", this))
    , m_copyButton(new QPushButton("Copy Error", this))
    , m_okButton(new QPushButton("OK", this))
    , m_topLayout(new QVBoxLayout)
    , m_errorLayout(new QHBoxLayout)
    , m_detailsGroupBoxLayout(new QVBoxLayout)
    , m_detailsButtonLayout(new QHBoxLayout)
    , m_buttonLayout(new QHBoxLayout)
{
	setupUI();
	m_errorLocationLabel->hide();

	m_applicationInfoButton->hide();
	m_versionInfoButton->hide();
	m_buildInfoButton->hide();
	m_hostInfoButton->hide();
	m_StackTraceButton->hide();
}

//--------------------------------------------------------------------------------------------------
//	ExceptionDialog (public ) []
//--------------------------------------------------------------------------------------------------
ExceptionDialog::ExceptionDialog(QString msg, QString details, bool fatal /*= false*/, QWidget* parent /*= nullptr*/)
    : QDialog(parent)
    , m_fatal(fatal)
    , m_errorMessage(msg)
    , m_errorDetails(details)
    , m_filename("")
    , m_function("")
    , m_line("")
    , m_errorIcon(new QLabel(this))
    , m_errorMessageLabel(new QLabel(m_errorMessage.prepend("FATAL ERROR: "), this))
    , m_errorLocationLabel(new QLabel(QString("in: %1 at: %2:%3").arg(m_function).arg(m_filename).arg(m_line), this))
    , m_detailsGroupBox(new QGroupBox())
    , m_detailsTextBrowser(new CorrectlySizedTextBrowser(this))
    , m_applicationInfoButton(new QPushButton("App Info"))
    , m_versionInfoButton(new QPushButton("Version Info"))
    , m_buildInfoButton(new QPushButton("Build Info"))
    , m_hostInfoButton(new QPushButton("Host Info"))
    , m_StackTraceButton(new QPushButton("Stack Trace"))
    , m_showDetailsButton(new QPushButton("Show Details", this))
    , m_copyButton(new QPushButton("Copy Error", this))
    , m_okButton(new QPushButton("OK", this))
    , m_topLayout(new QVBoxLayout)
    , m_errorLayout(new QHBoxLayout)
    , m_detailsGroupBoxLayout(new QVBoxLayout)
    , m_detailsButtonLayout(new QHBoxLayout)
    , m_buttonLayout(new QHBoxLayout)
{
	setupUI();
	m_errorLocationLabel->hide();
}

//--------------------------------------------------------------------------------------------------
//	setupUI (public ) []
//--------------------------------------------------------------------------------------------------
void ExceptionDialog::setupUI()
{
	m_fatal ? this->setWindowTitle("FATAL ERROR") : this->setWindowTitle("ERROR");
	m_errorDetails.prepend("ERROR: ");
	this->setLayout(m_topLayout);

	m_topLayout->addLayout(m_errorLayout);
	m_topLayout->addWidget(m_detailsGroupBox);
	m_topLayout->addLayout(m_buttonLayout);

	m_errorLayout->addWidget(m_errorIcon);
	if (m_fatal)
		m_errorIcon->setPixmap(style()->standardIcon(QStyle::SP_MessageBoxCritical).pixmap(QSize(50, 50)));
	else
		m_errorIcon->setPixmap(style()->standardIcon(QStyle::SP_MessageBoxWarning).pixmap(QSize(50, 50)));

	QFont monospaceFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
	monospaceFont.setPointSizeF(QApplication::font().pointSizeF() * 1.5);
	monospaceFont.setWeight(QFont::Weight::Medium);

	m_errorLayout->addWidget(m_errorMessageLabel);
	m_errorMessageLabel->setFont(monospaceFont);
	m_errorLayout->addSpacerItem(new QSpacerItem(0, 0, QSizePolicy::MinimumExpanding, QSizePolicy::Maximum));

	m_detailsGroupBox->setLayout(m_detailsGroupBoxLayout);
	m_detailsGroupBoxLayout->addWidget(m_errorLocationLabel);
	m_detailsGroupBoxLayout->addWidget(m_detailsTextBrowser);
	m_detailsGroupBoxLayout->addLayout(m_detailsButtonLayout);
	m_detailsGroupBox->setHidden(true);

	m_detailsButtonLayout->addWidget(m_applicationInfoButton);
	m_detailsButtonLayout->addWidget(m_versionInfoButton);
	m_detailsButtonLayout->addWidget(m_buildInfoButton);
	m_detailsButtonLayout->addWidget(m_hostInfoButton);
	m_detailsButtonLayout->addWidget(m_StackTraceButton);

	m_buttonLayout->addWidget(m_showDetailsButton);
	m_buttonLayout->addWidget(m_copyButton);
	m_buttonLayout->addSpacerItem(new QSpacerItem(0, 0, QSizePolicy::MinimumExpanding, QSizePolicy::Maximum));
	m_buttonLayout->addWidget(m_okButton);

	m_okButton->setFocus();

	m_errorMessageLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	m_detailsTextBrowser->setText(m_errorDetails);
	m_detailsTextBrowser->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
	m_detailsTextBrowser->verticalScrollBar()->setRange(0, m_detailsTextBrowser->document()->lineCount());
	m_detailsTextBrowser->verticalScrollBar()->setSingleStep(5);
	m_detailsTextBrowser->setFixedHeight(QFontMetrics(m_detailsTextBrowser->font()).lineSpacing() * 12
	                                     + m_detailsTextBrowser->document()->documentMargin()
	                                     + m_detailsTextBrowser->frameWidth() * 2);

	this->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

	VERIFY(connect(m_showDetailsButton, &QPushButton::clicked, this, &ExceptionDialog::on_pbDetails_clicked));
	VERIFY(connect(m_copyButton, &QPushButton::clicked, this, &ExceptionDialog::on_pbCopy_clicked));
	VERIFY(connect(m_okButton, &QPushButton::clicked, this, &ExceptionDialog::on_pbOK_clicked));

	VERIFY(connect(m_applicationInfoButton, &QPushButton::clicked, this, &ExceptionDialog::on_pbApplicationInfoButton_clicked));
	VERIFY(connect(m_versionInfoButton, &QPushButton::clicked, this, &ExceptionDialog::on_pbVersionInfoButton_clicked));
	VERIFY(connect(m_buildInfoButton, &QPushButton::clicked, this, &ExceptionDialog::on_pbBuildInfoButton_clicked));
	VERIFY(connect(m_hostInfoButton, &QPushButton::clicked, this, &ExceptionDialog::on_pbHostInfoButton_clicked));
	VERIFY(connect(m_StackTraceButton, &QPushButton::clicked, this, &ExceptionDialog::on_pbStackTraceButton_clicked));
}

//--------------------------------------------------------------------------------------------------
//	DESTRUCTOR
//--------------------------------------------------------------------------------------------------
ExceptionDialog::~ExceptionDialog()
{
}

//--------------------------------------------------------------------------------------------------
//	on_pbCopy_clicked (public ) []
//--------------------------------------------------------------------------------------------------
void ExceptionDialog::on_pbCopy_clicked()
{
	QApplication::clipboard()->setText(m_errorDetails);
}

//--------------------------------------------------------------------------------------------------
//	on_pbExit_clicked () []
//--------------------------------------------------------------------------------------------------
void ExceptionDialog::on_pbOK_clicked()
{
	this->close();
}

//--------------------------------------------------------------------------------------------------
//	on_pbDetails_clicked () []
//--------------------------------------------------------------------------------------------------
void ExceptionDialog::on_pbDetails_clicked()
{
	m_detailsGroupBox->setHidden(!m_detailsGroupBox->isHidden());

	if (!m_detailsGroupBox->isHidden())
		m_StackTraceButton->click();

	this->adjustSize();
}

//--------------------------------------------------------------------------------------------------
//	on_pbApplicationInfoButton_clicked () []
//--------------------------------------------------------------------------------------------------
void ExceptionDialog::on_pbApplicationInfoButton_clicked()
{
	auto cur = m_detailsTextBrowser->document()->find("APPLICATION INFO:");
	m_detailsTextBrowser->moveCursor(QTextCursor::End);
	m_detailsTextBrowser->setTextCursor(cur);
}

//--------------------------------------------------------------------------------------------------
//	on_pbVersionInfoButton_clicked () []
//--------------------------------------------------------------------------------------------------
void ExceptionDialog::on_pbVersionInfoButton_clicked()
{
	auto cur = m_detailsTextBrowser->document()->find("VERSION INFO:");
	m_detailsTextBrowser->moveCursor(QTextCursor::End);
	m_detailsTextBrowser->setTextCursor(cur);
}

//--------------------------------------------------------------------------------------------------
//	on_pbBuildInfoButton_clicked () []
//--------------------------------------------------------------------------------------------------
void ExceptionDialog::on_pbBuildInfoButton_clicked()
{
	auto cur = m_detailsTextBrowser->document()->find("BUILD INFO:");
	m_detailsTextBrowser->moveCursor(QTextCursor::End);
	m_detailsTextBrowser->setTextCursor(cur);
}

//--------------------------------------------------------------------------------------------------
//	on_pbHostInfoButton_clicked () []
//--------------------------------------------------------------------------------------------------
void ExceptionDialog::on_pbHostInfoButton_clicked()
{
	auto cur = m_detailsTextBrowser->document()->find("HOST INFO:");
	m_detailsTextBrowser->moveCursor(QTextCursor::End);
	m_detailsTextBrowser->setTextCursor(cur);
}

//--------------------------------------------------------------------------------------------------
//	on_pbStackTraceButton_clicked () []
//--------------------------------------------------------------------------------------------------
void ExceptionDialog::on_pbStackTraceButton_clicked()
{
	auto cur = m_detailsTextBrowser->document()->find("STACK TRACE:");
	m_detailsTextBrowser->moveCursor(QTextCursor::End);
	m_detailsTextBrowser->setTextCursor(cur);
}

//--------------------------------------------------------------------------------------------------
//	CorrectlySizedTextBrowser (public ) []
//--------------------------------------------------------------------------------------------------
CorrectlySizedTextBrowser::CorrectlySizedTextBrowser(QWidget* parent /*= nullptr*/)
    : QTextBrowser(parent)
{
	VERIFY(connect(this, &CorrectlySizedTextBrowser::textChanged, this, &CorrectlySizedTextBrowser::updateGeometry));
}

//--------------------------------------------------------------------------------------------------
//	sizeHint (public ) []
//--------------------------------------------------------------------------------------------------
QSize CorrectlySizedTextBrowser::sizeHint() const
{
	auto font     = this->currentFont();
	auto textSize = QFontMetrics(font).size(0, this->document()->toPlainText());
	int  newWidth = textSize.width() + 30;
	return QSize(newWidth, QTextBrowser::sizeHint().height());
}

//--------------------------------------------------------------------------------------------------
//	minimumSizeHint (public ) []
//--------------------------------------------------------------------------------------------------
QSize CorrectlySizedTextBrowser::minimumSizeHint() const
{
	return sizeHint();
}
