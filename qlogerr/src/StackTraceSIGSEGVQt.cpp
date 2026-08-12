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
	const StackTrace trace(7);    // determined empirically
#else
	const StackTrace trace(2);    // determined empirically
#endif

	QString time = "\n\nTIME:\n\n";
	time += "    Start Time   : " + QAPPINFO::applicationStartTime().toString() + "\n";
	time += "    Crash Time   : " + QDateTime::currentDateTime().toString() + "\n";
	time += "\n";

	const QString sDetails = QString("%1 Crashed! :'(").arg(QAPPINFO::name()) + time + QAPPINFO::systemDetails() + QString("STACK TRACE:\n\n") + trace.data();
	LOGERR << sDetails.toLocal8Bit().constData() << std::endl;

	// make sure the directory exists
	const QDir dir;
	dir.mkpath(QAPPINFO::crashDumpDir());
	LOGINFO << "Writing crash dump to: " << QAPPINFO::crashDumpDir().toStdString() << std::endl;

	QString crashdumpFileName = QString("crashdump-") + QDateTime::currentDateTime().toString(Qt::ISODate).remove(':') + ".txt";
	if (!qApp->applicationName().isEmpty())
		crashdumpFileName.prepend(qApp->applicationName() + '-');

	// write a dedicated crash dump file too for good measure
	if (QFile crashDumpFile(QAPPINFO::crashDumpDir() + '/' + crashdumpFileName); crashDumpFile.open(QIODevice::WriteOnly))
	{
		crashDumpFile.write(sDetails.toLocal8Bit());
		crashDumpFile.close();
	}

	LOGINFO << QAPPINFO::name().toLocal8Bit().constData() << " terminated due to a fatal error (application crash). Exiting with code 1..." << std::endl;

	// The crash is now fully recorded (logged above + written to the crash-dump file), so termination is safe with or
	// without a dialog. Show the interactive dialog ONLY when a human can actually dismiss it: a MODAL dialog raised
	// from a crash on a display-less run blocks forever, turning a crash into a silent hang (the process never exits).
	// Suppress it when there is no interactive display — the offscreen QPA platform (headless CI / automated tests) or
	// a non-GUI application — or when the host opts out via LOGERR_SUPPRESS_CRASH_DIALOG (an unattended run that does
	// have a display, e.g. one driven by an automation harness). All three are general, host-agnostic signals.
	const auto* guiApp        = qobject_cast<QGuiApplication*>(QCoreApplication::instance());
	const bool  hasDisplay    = guiApp && guiApp->platformName() != QLatin1String("offscreen");
	const bool  suppressByEnv = qEnvironmentVariableIsSet("LOGERR_SUPPRESS_CRASH_DIALOG");
	if (hasDisplay && !suppressByEnv)
	{
		ExceptionDialog dialog(QString("%1 Crashed.\nThe application will now terminate.").arg(QAPPINFO::name()), sDetails, true);
		dialog.exec();
	}

	std::exit(1);
}
