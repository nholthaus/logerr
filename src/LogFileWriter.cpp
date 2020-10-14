#include <LogFileWriter.h>
#include <logerr>

#include <chrono>

#include <appinfo.h>

#include <QDebug>
#include <QDir>
#include <QFile>

using namespace std::chrono_literals;

//--------------------------------------------------------------------------------------------------
//	LogFileWriter (public ) []
//--------------------------------------------------------------------------------------------------
LogFileWriter::LogFileWriter(QString logFilePath)
{
	m_logQueue.setTimeout(100ms);

	bool finishedConstructing = false;
	std::mutex constructorMutex;
	std::condition_variable finishedConstructingCV;

	m_thread = std::thread([&, this]
		{


			if (logFilePath.isEmpty())
				logFilePath = QString(APPINFO::logDir())
				.append(APPINFO::name())
				.append("-log-")
				.append(QDateTime::currentDateTime().toString(Qt::ISODate).remove(':'))
				.append(".txt");

			qDebug() << logFilePath;
			QFile logFile(logFilePath);

			QDir dir;
			if (!dir.mkpath(APPINFO::logDir()))
			{
				qDebug() << "failed mkpath";
				TODO("this is an error, we couldn't make the path to the log file");
			}


			if (!logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Unbuffered))
			{
				qDebug() << "failed open";
				TODO("this is an error, we couldn't make the path to the log file");
			}

			// at this point the thread is in steady-state
			std::unique_lock<std::mutex> lock(constructorMutex);
			finishedConstructing = true;
			finishedConstructingCV.notify_one();
			lock.unlock();

			std::string logEntry;
			while (!m_joinAll)
			{
				while (m_logQueue.pop(logEntry))
				{
					logFile.write(logEntry.c_str());
				}
			}

		});

	// wait until the thread hits steady-state to leave the constructor. This will lead to more
	// intuitive behavior (basically the class will behave the same way as if it wasn't threaded).
	std::unique_lock<std::mutex> lock(constructorMutex);
	finishedConstructingCV.wait(lock, [&finishedConstructing] { return finishedConstructing; });
}

//--------------------------------------------------------------------------------------------------
//	~LogFileWriter (public ) [virtual ]
//--------------------------------------------------------------------------------------------------
LogFileWriter::~LogFileWriter()
{
	m_joinAll.store(true);
	if (m_thread.joinable())
		m_thread.join();
}

//--------------------------------------------------------------------------------------------------
//	queueLogEntry (public ) []
//--------------------------------------------------------------------------------------------------
void LogFileWriter::queueLogEntry(std::string str)
{
	m_logQueue.emplace(std::move(str));
}

