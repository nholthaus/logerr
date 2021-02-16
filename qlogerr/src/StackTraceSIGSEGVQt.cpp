//----------------------------
//  INCLUDES
//----------------------------

#include <ExceptionDialog.h>
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <StackTrace.h>
#include <logerrMacros.h>
#include <qappinfo.h>

//--------------------------------------------------------------------------------------------------
//	stackTraceSigSev (public ) [static ]
//--------------------------------------------------------------------------------------------------
void stackTraceSIGSEGVQt(int)
{
	// Gather the details
#ifdef _MSC_VER
	StackTrace trace(7);    // determined empirically
#else
	StackTrace trace(2);    // determined empirically
#endif

	QString time = "\n\nTIME:\n\n";
	time += "    Start Time   : " + QAPPINFO::applicationStartTime().toString() + "\n";
	time += "    Crash Time   : " + QDateTime::currentDateTime().toString() + "\n";
	time += "\n";

	QString sDetails = QString("%1 Crashed! :'(").arg(QAPPINFO::name()) + time + QAPPINFO::systemDetails() + QString("STACK TRACE:\n\n") + trace.data();
	LOGERR << sDetails.toLocal8Bit().constData() << std::endl;

	// make sure the directory exists
	QDir dir;
	dir.mkpath(QAPPINFO::crashDumpDir());
	LOGINFO << "Writing crash dump to: " << QAPPINFO::crashDumpDir().toStdString() << std::endl;

	QString crashdumpFileName = QString("crashdump-") + QDateTime::currentDateTime().toString(Qt::ISODate).remove(':') + ".txt";
	if (!qApp->applicationName().isEmpty())
		crashdumpFileName.prepend(qApp->applicationName() + '-');

	// write a dedicated crash dump file too for good measure
	QFile crashDumpFile(QAPPINFO::crashDumpDir() + '/' + crashdumpFileName);
	crashDumpFile.open(QIODevice::WriteOnly);
	crashDumpFile.write(sDetails.toLocal8Bit());
	crashDumpFile.close();

	LOGINFO << QAPPINFO::name().toLocal8Bit().constData() << " terminated due to a fatal error (application crash). Exiting with code 1..." << std::endl;

	// try to show a dialog
	ExceptionDialog dialog(QString("%1 Crashed.\nThe application will now terminate.").arg(QAPPINFO::name()), sDetails, true);
	dialog.exec();

	std::exit(1);
}