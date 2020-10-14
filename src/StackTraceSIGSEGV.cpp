
#include <StackTrace.h>
#include <appinfo.h>
#include <QApplication>
#include <QDir>
#include <logerrMacros.h>
#include <ExceptionDialog.h>

//--------------------------------------------------------------------------------------------------
//	stackTraceSigSev (public ) [static ]
//--------------------------------------------------------------------------------------------------
void stackTraceSIGSEGV(int sig)
{
	// Gather the details
#ifdef _MSC_VER
	StackTrace trace(7); // determined empirically
#else
	StackTrace trace(2); // determined empirically
#endif

	QString time = "\n\nTIME:\n\n";
	time += "    Start Time   : " + APPINFO::applicationStartTime().toString() + "\n";
	time += "    Crash Time   : " + QDateTime::currentDateTime().toString() + "\n";
	time += "\n";

	QString sDetails = QString("%1 Crashed! :'(").arg(APPINFO::name()) + time + APPINFO::systemDetails() + QString("STACK TRACE:\n\n") + trace;
	LOGERR << sDetails.toLocal8Bit().constData() << std::endl;

	// make sure the directory exists
	QDir dir;
	dir.mkpath(APPINFO::crashDumpDir());
	LOGINFO << "Writing crash dump to: " << APPINFO::crashDumpDir().toStdString() << std::endl;

	QString crashdumpFileName = QString("crashdump-") + QDateTime::currentDateTime().toString(Qt::ISODate).remove(':') + ".txt";
	if (!qApp->applicationName().isEmpty())
		crashdumpFileName.prepend(qApp->applicationName() + '-');

	// write a dedicated crash dump file too for good measure
	QFile crashDumpFile(APPINFO::crashDumpDir() + '/' + crashdumpFileName);
	crashDumpFile.open(QIODevice::WriteOnly);
	crashDumpFile.write(sDetails.toLocal8Bit());
	crashDumpFile.close();

	LOGINFO << APPINFO::name().toLocal8Bit().constData() << " terminated due to a fatal error (application crash). Exiting with code 1..." << std::endl;

	// try to show a dialog
	ExceptionDialog dialog(QString("%1 Crashed.\nThe application will now terminate.").arg(APPINFO::name()), sDetails, true);
	dialog.exec();

	std::exit(1);
}

//--------------------------------------------------------------------------------------------------
//	CrashAndBurn (public ) []
//--------------------------------------------------------------------------------------------------
void CrashAndBurn()
{
	int* crash = nullptr;
	*crash = 0xDEAD;
}

