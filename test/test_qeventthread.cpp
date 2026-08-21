#include <Application.h>
#include <ExceptionDialog.h>
#include <LogModel.h>
#include <LogProxyModel.h>
#include <LogFileWriter.h>
#include <LogStream.h>
#include <QEventThread.h>
#include <logBlaster.h>
#include <logChannel.h>
#include <logDock.h>
#include <logReceiver.h>
#include <logerr>
#include <logerrGuiApplication.h>
#include <logerrThread.h>
#include <qCoreAppThread.h>
#include <timestampLite.h>
#include <concurrent_queue.h>
#include <gtest/gtest.h>

#include <QApplication>
#include <QBrush>
#include <QCheckBox>
#include <QClipboard>
#include <QEventLoop>
#include <QGroupBox>
#include <QGuiApplication>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkDatagram>
#include <QObject>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>
#include <QTextBrowser>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>
#include <QUdpSocket>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <memory>
#include <map>
#include <numeric>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

using namespace std::chrono_literals;

namespace
{
	#define CHECK(condition) EXPECT_TRUE(condition)

	bool runUntil(const std::function<bool()>& done, std::chrono::milliseconds timeout = 5s)
	{
		if (done())
			return true;

		QEventLoop loop;
		QTimer poll;
		QTimer deadline;
		poll.setInterval(1ms);
		deadline.setSingleShot(true);
		QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
			if (done())
				loop.quit();
		});
		QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
		poll.start();
		deadline.start(timeout);
		loop.exec();
		return done();
	}

	class DtorProbe : public QObject
	{
	public:
		DtorProbe(std::atomic<QThread*>& destroyedOn, QObject* parent)
		    : QObject(parent), m_destroyedOn(destroyedOn)
		{
		}

		~DtorProbe() override { m_destroyedOn.store(QThread::currentThread()); }

	private:
		std::atomic<QThread*>& m_destroyedOn;
	};

	void closeActiveModalWidget()
	{
		if (auto* widget = QApplication::activeModalWidget())
			widget->close();
		else
			QTimer::singleShot(1, &closeActiveModalWidget);
	}

	class ThrowingEventTarget : public QObject
	{
	public:
		enum class Behavior { Accept, NonFatalLogerr, FatalLogerr, StandardException, UnknownException };
		explicit ThrowingEventTarget(Behavior behavior) : behavior(behavior) {}

		bool event(QEvent*) override
		{
			switch (behavior)
			{
				case Behavior::Accept: return true;
				case Behavior::NonFatalLogerr: ERR("nonfatal GUI event failure");
				case Behavior::FatalLogerr: FATAL_ERR("fatal GUI event failure");
				case Behavior::StandardException: throw std::runtime_error("standard GUI event failure");
				case Behavior::UnknownException: throw 42;
			}
			return false;
		}

		Behavior behavior;
	};

	void testConcurrentQueue()
	{
		concurrent_queue<int> queue;
		CHECK(queue.empty());
		queue.push(1);
		queue.emplace(2);
		queue.push(3);
		CHECK(queue.size() == 3);
		for (int expected = 1; expected <= 3; ++expected)
		{
			int value = 0;
			CHECK(queue.try_pop(value));
			CHECK(value == expected);
		}
		int value = 0;
		CHECK(!queue.try_pop(value));
		CHECK(!queue.try_pop_for(value, 5ms));

		std::thread delayedProducer([&] {
			QThread::msleep(20);
			queue.push(42);
		});
		CHECK(queue.try_pop_for(value, 2s));
		CHECK(value == 42);
		delayedProducer.join();

		constexpr int producers = 4;
		constexpr int perProducer = 1000;
		std::vector<std::thread> threads;
		threads.reserve(producers);
		for (int producer = 0; producer < producers; ++producer)
			threads.emplace_back([&, producer] {
				for (int i = 0; i < perProducer; ++i)
					queue.push(producer * perProducer + i);
			});
		for (auto& thread : threads)
			thread.join();
		std::vector<bool> seen(static_cast<size_t>(producers * perProducer));
		while (queue.try_pop(value))
		{
			CHECK(value >= 0 && value < producers * perProducer);
			if (value >= 0 && value < producers * perProducer)
				seen[static_cast<size_t>(value)] = true;
		}
		CHECK(std::all_of(seen.begin(), seen.end(), [](bool item) { return item; }));

		concurrent_queue<int> original{3, 7};
		concurrent_queue<int> copy = original;
		CHECK(copy == original);
		concurrent_queue<int> other;
		other.push(9);
		swap(copy, other);
		CHECK(copy.size() == 1);
		copy.clear();
		CHECK(copy.empty());

		concurrent_queue<int> stoppable;
		std::stop_source stopSource;
		std::atomic_bool waitReturned{false};
		std::jthread waiter([&] {
			int ignored = 0;
			CHECK(!stoppable.wait_pop(ignored, stopSource.get_token()));
			waitReturned.store(true);
		});
		stopSource.request_stop();
		waiter.join();
		CHECK(waitReturned.load());
	}

	void testLogStream()
	{
		std::ostringstream stream;
		std::vector<std::string> first;
		std::vector<std::string> second;
		{
			LogStream logger(stream);
			logger.registerLogFunction("first", [&](std::string value) { first.push_back(std::move(value)); });
			logger.registerLogFunction("second", [&](std::string value) { second.push_back(std::move(value)); });
			stream << "line one" << '\n';
			CHECK(first == std::vector<std::string>{"line one\n"});
			CHECK(second == first);
			logger.unregisterLogFunction("second");
			stream << "partial line";
		} // destructor flushes the partial line and restores the original stream buffer
		CHECK(first == (std::vector<std::string>{"line one\n", "partial line"}));
		CHECK(second == std::vector<std::string>{"line one\n"});
		stream << "restored";
		CHECK(stream.str() == "restored");
	}

	void testTimestampLite()
	{
		TimestampLite timestamp;
		const auto timePoint = static_cast<std::chrono::system_clock::time_point>(timestamp);
		const auto time = static_cast<std::time_t>(timestamp);
		const std::string text = timestamp;
		std::ostringstream streamed;
		streamed << timestamp;
		CHECK(std::chrono::system_clock::to_time_t(timePoint) == time);
		CHECK(streamed.str() == text);
		CHECK(std::regex_match(text, std::regex(R"(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{9} .+)")));
	}

	void testErrorMacrosAndMetadata()
	{
		const size_t expectedLine = __LINE__ + 3;
		try
		{
			ERR("ordinary failure");
		}
		catch (const logerr::exception& error)
		{
			CHECK(error.errorMessage() == "ordinary failure");
			CHECK(error.filename() == "test_qeventthread.cpp");
			CHECK(error.line() == expectedLine);
			CHECK(!error.fatal());
			CHECK(error.function().find("testErrorMacrosAndMetadata") != std::string::npos);
			CHECK(std::string(error.what()).find("ordinary failure") != std::string::npos);
		}
		try
		{
			FATAL_ERR("fatal failure");
		}
		catch (const logerr::exception& error)
		{
			CHECK(error.fatal());
			CHECK(std::string(error.what()).starts_with("FATAL "));
		}
	}

	void testLogFileWriter()
	{
		const auto path = std::filesystem::temp_directory_path() /
		                  ("logerr-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".txt");
		{
			LogFileWriter writer(path.string());
			// filePath() reports the exact file this writer opened, populated by the time the (blocking) ctor returns,
			// so a host can open THIS process's live log instead of guessing the newest file in a shared directory.
			CHECK(writer.filePath() == path.string());
			writer.write("first\n");
			writer.write("second\n");
		} // destructor drains accepted writes before closing the file
		std::ifstream file(path);
		std::string first;
		std::string second;
		std::string extra;
		CHECK(static_cast<bool>(std::getline(file, first)));
		CHECK(static_cast<bool>(std::getline(file, second)));
		CHECK(!static_cast<bool>(std::getline(file, extra)));
		CHECK(first == "first");
		CHECK(second == "second");
		std::error_code ignored;
		std::filesystem::remove(path, ignored);
	}

	void testLogFileWriterDeduplicatesRepeatedTraceFooters()
	{
		const auto path = std::filesystem::temp_directory_path() /
		                  ("logerr-dedup-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".txt");
		// A frame line matches the "[<n>] ... 0x..." shape formatFrames emits; two entries carry the IDENTICAL footer,
		// one carries a DIFFERENT footer. The file writer must record the first full, collapse the repeat to the note,
		// and record the different footer in full - the on-disk dedup contract (the GUI dock, a separate sink, is
		// unaffected: it never sees the file writer).
		const std::string footerA =
		    "    [0  ]   0x00007ff000000001: a.cpp:10                | foo\n"
		    "    [1  ]   0x00007ff000000002: b.cpp:20                | bar\n";
		const std::string footerB =
		    "    [0  ]   0x00007ff000000003: c.cpp:30                | baz\n";
		{
			LogFileWriter writer(path.string());
			writer.write("[t] [m] [ERROR]    boom\n" + footerA);    // first: full
			writer.write("[t] [m] [ERROR]    boom again\n" + footerA);    // repeat of footerA: collapsed
			writer.write("[t] [m] [ERROR]    different\n" + footerB);    // new footer: full
		}
		std::ifstream    file(path);
		const std::string contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		// footerA's frames appear exactly once (first entry); the repeat became the note; footerB's frame appears once.
		const auto countOccurrences = [&](const std::string& needle)
		{
			std::size_t n = 0, pos = 0;
			while ((pos = contents.find(needle, pos)) != std::string::npos) { ++n; pos += needle.size(); }
			return n;
		};
		CHECK(countOccurrences("a.cpp:10") == 1);    // footerA traced once
		CHECK(countOccurrences("c.cpp:30") == 1);    // footerB traced once
		CHECK(countOccurrences("trace deduplicated") == 1);    // the single repeat noted
		CHECK(contents.find("boom again") != std::string::npos);    // the repeat's MESSAGE is still written
		std::error_code ignored;
		std::filesystem::remove(path, ignored);
	}

	void testLogerrThreadStopAndExceptionSafety()
	{
		std::atomic_bool stopped{false};
		{
			logerr::thread worker([&](std::stop_token stop) {
				while (!stop.stop_requested())
					std::this_thread::yield();
				stopped.store(stop.stop_requested());
			});
		} // jthread requests stop and joins automatically
		CHECK(stopped.load());

		{
			const std::lock_guard<std::mutex> lock(g_exceptionMutex);
			g_exceptionPtr = nullptr;
		}
		{
			logerr::thread worker([] { throw std::runtime_error("worker failure"); });
		} // exception is captured; none escapes the thread entry point
		std::exception_ptr captured;
		{
			const std::lock_guard<std::mutex> lock(g_exceptionMutex);
			captured = std::exchange(g_exceptionPtr, nullptr);
		}
		CHECK(captured != nullptr);
		try
		{
			std::rethrow_exception(captured);
		}
		catch (const std::runtime_error& error)
		{
			CHECK(std::string(error.what()) == "worker failure");
		}
	}

	void testPrimitiveAffinityAndOrdering()
	{
		std::atomic<QThread*> setupThread{nullptr};
		std::atomic<QThread*> destroyedOn{nullptr};
		std::vector<int>      seen;
		{
			QEventThread<int> worker = THREAD_SETUP_CAPTURE([&], {
				setupThread.store(QThread::currentThread());
				new DtorProbe(destroyedOn, eventLoop);
				ON_DATA_RECEIVED_CAPTURE([&], { seen.push_back(data); });
			});

			for (int i = 0; i < 1000; ++i)
				worker.enqueue(i);

			auto barrier = std::make_shared<std::promise<void>>();
			auto done    = barrier->get_future();
			worker.runOnThread([barrier] { barrier->set_value(); });
			CHECK(done.wait_for(5s) == std::future_status::ready);
		}

		CHECK(setupThread.load() != nullptr);
		CHECK(setupThread.load() != QThread::currentThread());
		CHECK(destroyedOn.load() == setupThread.load());
		CHECK(seen.size() == 1000);
		for (int i = 0; i < static_cast<int>(seen.size()); ++i)
			CHECK(seen[static_cast<size_t>(i)] == i);
	}

	void testLambdaErrorHasUsefulFunctionContext()
	{
		auto namedAtSource = [] {
			try
			{
				ERR("lambda context probe");
			}
			catch (const logerr::exception& error)
			{
				return error.function();
			}
			return std::string{};
		};

		const std::string function = namedAtSource();
		CHECK(function.find("testLambdaErrorHasUsefulFunctionContext") != std::string::npos);
		CHECK(function.find("lambda") != std::string::npos || function.find("operator") != std::string::npos);
	}

	void testPrimitiveOutputAndFastTeardown()
	{
		std::vector<int> outputs;
		QThread*        owner = QThread::currentThread();
		QEventThread<int, int> worker = THREAD_SETUP_CAPTURE([&], {
			ON_DATA_RECEIVED_CAPTURE([&], { EMIT(data * 2); });
		});
		worker.onOutput([&](int& value) {
			CHECK(QThread::currentThread() == owner);
			outputs.push_back(value);
		});
		for (int i = 0; i < 250; ++i)
			worker.enqueue(i);
		CHECK(runUntil([&] { return outputs.size() == 250; }));
		for (int i = 0; i < static_cast<int>(outputs.size()); ++i)
			CHECK(outputs[static_cast<size_t>(i)] == i * 2);

		for (int i = 0; i < 200; ++i)
			QEventThread<> fast(THREAD_SETUP_CAPTURE([&], {}), QEventThread<>::StartMode::NonBlocking);

		std::atomic_int preRegistrationCount{0};
		QEventThread<int, int> lateHandler = THREAD_SETUP_CAPTURE([&], {
			ON_DATA_RECEIVED_CAPTURE([&], { EMIT(data); });
		});
		for (int i = 0; i < 100; ++i)
			lateHandler.enqueue(i);
		auto workerBarrier = std::make_shared<std::promise<void>>();
		auto workerDone    = workerBarrier->get_future();
		lateHandler.runOnThread([workerBarrier] { workerBarrier->set_value(); });
		CHECK(workerDone.wait_for(5s) == std::future_status::ready);
		lateHandler.onOutput([&](int&) { preRegistrationCount.fetch_add(1); });
		CHECK(preRegistrationCount.load() == 100);

	}

	void testLogModelParsingAndBurst()
	{
		LogModel model;
		model.queueLogEntry("[2026-08-12] [module] [WARNING] message\ndetail one\ndetail two");
		model.queueLogEntry("raw line\nraw detail");
		model.queueLogEntry("   ");
		for (int i = 0; i < 500; ++i)
			model.queueLogEntry("[t] [burst] [INFO] row " + std::to_string(i) + "\n");

		CHECK(runUntil([&] { return model.rowCount() == 502; }));
		CHECK(model.data(model.index(0, LogModel::Column::Module)).toString() == "module");
		CHECK(model.data(model.index(0, LogModel::Column::Type)).toString() == "WARNING");
		CHECK(model.data(model.index(0, LogModel::Column::Message)).toString() == "message");
		CHECK(model.rowCount(model.index(0, 0)) == 2);
		CHECK(model.data(model.index(1, LogModel::Column::Module)).toString() == "unset_name");
		CHECK(model.data(model.index(501, LogModel::Column::Message)).toString() == "row 499");

		model.setScrollbackBufferSize(100);
		CHECK(model.rowCount() == 100);
		for (int i = 0; i < 100; ++i)
		{
			LogModel transient;
			transient.queueLogEntry("[t] [m] [INFO] teardown\n");
		}
	}

	void testLogBlasterFlushesBeforeDestruction()
	{
		QUdpSocket receiver;
		CHECK(receiver.bind(QHostAddress::LocalHost, 0));
		constexpr int count = 200;
		{
			LogBlaster blaster(QHostAddress::LocalHost, receiver.localPort());
			for (int i = 0; i < count; ++i)
				blaster.blast("packet-" + std::to_string(i));
		} // ordered flush barrier must send every accepted packet before the socket is destroyed

		std::vector<std::string> received;
		CHECK(runUntil([&] {
			while (receiver.hasPendingDatagrams())
				received.push_back(receiver.receiveDatagram().data().toStdString());
			return received.size() == count;
		}));
		CHECK(received.size() == count);
		std::sort(received.begin(), received.end());
		std::vector<std::string> expected;
		expected.reserve(count);
		for (int i = 0; i < count; ++i)
			expected.push_back("packet-" + std::to_string(i));
		std::sort(expected.begin(), expected.end());
		CHECK(received == expected);
	}

	void testLogProxyModelFiltering()
	{
		LogModel source;
		source.appendRow(QStringLiteral("[t1] [alpha] [ERROR] error\n"));
		source.appendRow(QStringLiteral("[t2] [beta] [WARNING] warning\n"));
		source.appendRow(QStringLiteral("[t3] [gamma] [INFO] info\n"));
		source.appendRow(QStringLiteral("[t4] [delta] [DEBUG] debug\n"));
		LogProxyModel proxy;
		proxy.setSourceModel(&source);
		CHECK(proxy.rowCount() == 4);
		proxy.setAcceptsErrors(false);
		CHECK(!proxy.acceptsErrors());
		CHECK(proxy.rowCount() == 3);
		proxy.setAcceptsWarnings(false);
		proxy.setAcceptsInfo(false);
		CHECK(proxy.rowCount() == 1);
		proxy.setAcceptsDebug(false);
		CHECK(proxy.rowCount() == 0);
		proxy.setAcceptsErrors(true);
		CHECK(proxy.rowCount() == 1);
		proxy.setAcceptsTimestamps(false);
		proxy.setAcceptsModules(false);
		CHECK(!proxy.acceptsTimestamps());
		CHECK(!proxy.acceptsModules());
	}

	void testLogChannelConfiguration()
	{
		const QByteArray oldPort = qgetenv("LOGERR_LOG_PORT");
		const QByteArray oldGroup = qgetenv("LOGERR_LOG_GROUP");
		qputenv("LOGERR_LOG_PORT", "41234");
		qputenv("LOGERR_LOG_GROUP", "239.1.2.3");
		CHECK(LogChannel::port() == 41234);
		CHECK(LogChannel::group() == QHostAddress("239.1.2.3"));
		qputenv("LOGERR_LOG_PORT", "invalid");
		CHECK(LogChannel::port() == LogChannel::kDefaultPort);
		if (oldPort.isNull())
			qunsetenv("LOGERR_LOG_PORT");
		else
			qputenv("LOGERR_LOG_PORT", oldPort);
		if (oldGroup.isNull())
			qunsetenv("LOGERR_LOG_GROUP");
		else
			qputenv("LOGERR_LOG_GROUP", oldGroup);
	}
}

class LogerrTest : public ::testing::Test {};

#define LOGERR_TEST(name, function) TEST_F(LogerrTest, name) { function(); }
LOGERR_TEST(ConcurrentQueue, testConcurrentQueue)
LOGERR_TEST(LogStream, testLogStream)
LOGERR_TEST(TimestampLite, testTimestampLite)
LOGERR_TEST(ErrorMacrosAndMetadata, testErrorMacrosAndMetadata)
LOGERR_TEST(LogFileWriter, testLogFileWriter)
LOGERR_TEST(LogFileWriterDeduplicatesRepeatedTraceFooters, testLogFileWriterDeduplicatesRepeatedTraceFooters)
LOGERR_TEST(LogerrThreadStopAndExceptionSafety, testLogerrThreadStopAndExceptionSafety)
LOGERR_TEST(LambdaErrorFunctionContext, testLambdaErrorHasUsefulFunctionContext)
LOGERR_TEST(QEventThreadAffinityAndOrdering, testPrimitiveAffinityAndOrdering)
LOGERR_TEST(QEventThreadOutputAndFastTeardown, testPrimitiveOutputAndFastTeardown)
LOGERR_TEST(LogModelParsingAndBurst, testLogModelParsingAndBurst)
LOGERR_TEST(LogProxyModelFiltering, testLogProxyModelFiltering)
LOGERR_TEST(LogChannelConfiguration, testLogChannelConfiguration)
LOGERR_TEST(LogBlasterFlushesBeforeDestruction, testLogBlasterFlushesBeforeDestruction)

class LogModelFixture : public ::testing::Test
{
protected:
	LogModel model;
};

TEST_F(LogModelFixture, StructureRolesHeadersAndEditingFollowTheModelContract)
{
	model.appendRow(QStringLiteral("[t0] [module] [ERROR] message\ndetail one\ndetail two"));
	model.appendRow(QStringLiteral("[t1] [module] [INFO] informational\n"));
	model.appendRow(QStringLiteral("[t2] [module] [WARNING] warning\n"));
	model.appendRow(QStringLiteral("[t3] [module] [DEBUG] debug\n"));

	EXPECT_EQ(model.rowCount(), 4);
	EXPECT_EQ(model.columnCount(), 4);
	EXPECT_TRUE(model.hasChildren());
	EXPECT_FALSE(model.parent({}).isValid());
	EXPECT_FALSE(model.index(-1, 0).isValid());
	EXPECT_FALSE(model.index(0, model.columnCount()).isValid());
	EXPECT_EQ(model.headerData(LogModel::Timestamp, Qt::Horizontal).toString(), "Timestamp");
	EXPECT_FALSE(model.headerData(0, Qt::Horizontal, Qt::ToolTipRole).isValid());

	const QModelIndex error = model.index(0, LogModel::Message);
	ASSERT_TRUE(error.isValid());
	EXPECT_EQ(model.data(error).toString(), "message");
	EXPECT_TRUE(model.data(error, Qt::FontRole).value<QFont>().bold());
	EXPECT_EQ(model.data(error, Qt::ForegroundRole).value<QBrush>().color(), QColor(Qt::red));
	EXPECT_FALSE(model.data({}, Qt::DisplayRole).isValid());
	EXPECT_FALSE(model.data(error, Qt::UserRole).isValid());

	const QModelIndex infoType = model.index(1, LogModel::Type);
	EXPECT_EQ(model.data(infoType, Qt::ForegroundRole).value<QBrush>().color(), QColor(Qt::gray));
	EXPECT_FALSE(model.data(infoType, Qt::FontRole).isValid());
	EXPECT_EQ(model.data(model.index(2, LogModel::Message), Qt::ForegroundRole).value<QBrush>().color(), QColor("#a67c00"));
	EXPECT_EQ(model.data(model.index(3, LogModel::Message), Qt::ForegroundRole).value<QBrush>().color(), QColor("#2db2e7"));

	const QModelIndex parent = model.index(0, 0);
	EXPECT_TRUE(model.hasChildren(parent));
	EXPECT_EQ(model.rowCount(parent), 2);
	const QModelIndex detail = model.index(1, LogModel::Message, parent);
	ASSERT_TRUE(detail.isValid());
	EXPECT_EQ(model.data(detail).toString(), "detail two");
	EXPECT_EQ(model.parent(detail), parent);
	EXPECT_EQ(model.data(model.index(0, LogModel::Module, parent)).toString(), "");

	QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
	EXPECT_TRUE(model.setData(error, QStringLiteral("edited")));
	EXPECT_EQ(model.data(error).toString(), "edited");
	EXPECT_EQ(changed.count(), 1);
	EXPECT_FALSE(model.setData({}, QStringLiteral("ignored")));
}

TEST_F(LogModelFixture, RawWhitespaceAndInsertionBoundariesAreHandled)
{
	QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
	model.appendRow(QStringLiteral("   \n\t"));
	EXPECT_EQ(model.rowCount(), 0);
	EXPECT_EQ(inserted.count(), 0);

	model.appendRow(std::string(" raw message \n detail "));
	ASSERT_EQ(model.rowCount(), 1);
	EXPECT_EQ(model.data(model.index(0, LogModel::Module)).toString(), "unset_name");
	EXPECT_EQ(model.data(model.index(0, LogModel::Type)).toString(), "INFO");
	EXPECT_EQ(model.data(model.index(0, LogModel::Message)).toString(), "raw message");
	EXPECT_EQ(model.rowCount(model.index(0, 0)), 1);

	inserted.clear();
	EXPECT_TRUE(model.insertRows(1, 2));
	ASSERT_EQ(inserted.count(), 1);
	EXPECT_EQ(inserted.at(0).at(1).toInt(), 1);
	EXPECT_EQ(inserted.at(0).at(2).toInt(), 2);
	EXPECT_EQ(model.data(model.index(1, LogModel::Message)).toString(), "");
	EXPECT_TRUE(model.setData(model.index(2, LogModel::Message), QStringLiteral("new row")));
	EXPECT_EQ(model.data(model.index(2, LogModel::Message)).toString(), "new row");
	EXPECT_FALSE(model.insertRows(-1, 1));
	EXPECT_FALSE(model.insertRows(model.rowCount() + 1, 1));
	EXPECT_FALSE(model.insertRows(0, 0));
	EXPECT_FALSE(model.insertRows(0, 1, model.index(0, 0)));
}

TEST_F(LogModelFixture, TracedErrorLocationRemainsPartOfTheMessage)
{
	model.appendRow(QStringLiteral("[t] [module] [ERROR] [source.cpp:42 function()] diagnostic\n"));

	ASSERT_EQ(model.rowCount(), 1);
	EXPECT_EQ(model.data(model.index(0, LogModel::Type)).toString(), "ERROR");
	EXPECT_EQ(model.data(model.index(0, LogModel::Message)).toString(), "[source.cpp:42 function()] diagnostic");
}

TEST_F(LogModelFixture, ScrollbackTrimsDirectAndQueuedRowsWithoutInvalidRanges)
{
	for (int i = 0; i < 5; ++i)
		model.appendRow(QString("[t] [m] [INFO] direct %1\n").arg(i));
	model.setScrollbackBufferSize(2);
	EXPECT_EQ(model.scrollbackBufferSize(), 2U);
	ASSERT_EQ(model.rowCount(), 2);
	EXPECT_EQ(model.data(model.index(0, LogModel::Message)).toString(), "direct 3");

	for (int i = 0; i < 4; ++i)
		model.queueLogEntry("[t] [m] [INFO] queued " + std::to_string(i) + "\n");
	ASSERT_TRUE(runUntil([&] {
		return model.rowCount() == 2 && model.data(model.index(1, LogModel::Message)).toString() == "queued 3";
	}));
	EXPECT_EQ(model.data(model.index(0, LogModel::Message)).toString(), "queued 2");

	model.setScrollbackBufferSize(0);
	EXPECT_EQ(model.rowCount(), 0);
	model.queueLogEntry("[t] [m] [INFO] discarded\n");
	QTest::qWait(350);
	EXPECT_EQ(model.rowCount(), 0);
}

class LogProxyModelFixture : public ::testing::Test
{
protected:
	void SetUp() override
	{
		source.appendRow(QStringLiteral("[t4] [delta] [DEBUG] zebra\n"));
		source.appendRow(QStringLiteral("[t1] [alpha] [ERROR] alpha\n"));
		source.appendRow(QStringLiteral("[t2] [beta] [WARNING] bravo\n"));
		source.appendRow(QStringLiteral("[t3] [gamma] [INFO] charlie\n"));
		proxy.setSourceModel(&source);
	}

	LogModel source;
	LogProxyModel proxy;
};

TEST_F(LogProxyModelFixture, EveryLevelFlagRefiltersRowsAndNoOpAssignmentsStayNoOps)
{
	EXPECT_TRUE(proxy.acceptsErrors());
	EXPECT_TRUE(proxy.acceptsWarnings());
	EXPECT_TRUE(proxy.acceptsInfo());
	EXPECT_TRUE(proxy.acceptsDebug());
	EXPECT_TRUE(proxy.acceptsTimestamps());
	EXPECT_TRUE(proxy.acceptsModules());
	EXPECT_EQ(proxy.rowCount(), 4);

	proxy.setAcceptsErrors(true);
	EXPECT_EQ(proxy.rowCount(), 4);
	proxy.setAcceptsErrors(false);
	proxy.setAcceptsWarnings(false);
	proxy.setAcceptsInfo(false);
	proxy.setAcceptsDebug(false);
	EXPECT_EQ(proxy.rowCount(), 0);
	proxy.setAcceptsErrors(true);
	proxy.setAcceptsWarnings(true);
	proxy.setAcceptsInfo(true);
	proxy.setAcceptsDebug(true);
	EXPECT_EQ(proxy.rowCount(), 4);

	proxy.setAcceptsTimestamps(false);
	proxy.setAcceptsModules(false);
	EXPECT_FALSE(proxy.acceptsTimestamps());
	EXPECT_FALSE(proxy.acceptsModules());
}

TEST_F(LogProxyModelFixture, InheritedTextFilteringAndSortingUseScalarCellText)
{
	proxy.setFilterKeyColumn(LogModel::Message);
	proxy.setFilterCaseSensitivity(Qt::CaseInsensitive);
	proxy.setFilterFixedString(QStringLiteral("BRAVO"));
	ASSERT_EQ(proxy.rowCount(), 1);
	EXPECT_EQ(proxy.data(proxy.index(0, LogModel::Message)).toString(), "bravo");

	proxy.setFilterRegularExpression(QRegularExpression(QStringLiteral("^(alpha|zebra)$")));
	EXPECT_EQ(proxy.rowCount(), 2);
	proxy.setFilterRegularExpression(QRegularExpression{});
	proxy.sort(LogModel::Message, Qt::AscendingOrder);
	ASSERT_EQ(proxy.rowCount(), 4);
	EXPECT_EQ(proxy.data(proxy.index(0, LogModel::Message)).toString(), "alpha");
	EXPECT_EQ(proxy.data(proxy.index(3, LogModel::Message)).toString(), "zebra");
}

class QtWidgetFixture : public ::testing::Test
{
protected:
	// LogDock now persists its filter/column/search preferences to QSettings and restores them on construction. Clear
	// the settings scope before each widget test so a LogDock starts from its documented defaults (all levels shown,
	// all columns visible) - otherwise a preference persisted by a prior test leaks in and silently filters the rows a
	// later test expects to see (an order-dependent failure). This is the polluter fix at the fixture, not per-test.
	void SetUp() override { QSettings().clear(); }
	void TearDown() override { QSettings().clear(); }

	template<class Widget>
	static Widget* childWithText(QObject& parent, const QString& text)
	{
		for (auto* child : parent.findChildren<Widget*>())
			if (child->text() == text)
				return child;
		return nullptr;
	}
};

TEST_F(QtWidgetFixture, LogDockControlsDriveFilteringColumnsSearchAndScrollback)
{
	LogDock dock;
	auto* view = dock.findChild<QTreeView*>();
	auto* search = dock.findChild<QLineEdit*>(QString{}, Qt::FindDirectChildrenOnly);
	ASSERT_NE(view, nullptr);
	for (auto* edit : dock.findChildren<QLineEdit*>())
		if (edit->placeholderText() == "Find...")
			search = edit;
	ASSERT_NE(search, nullptr);

	dock.queueLogEntry("[t] [m] [ERROR] first\n");
	dock.queueLogEntry("[t] [m] [INFO] second\n");
	ASSERT_TRUE(runUntil([&] { return view->model()->rowCount() == 2; }));

	auto* errors = childWithText<QCheckBox>(dock, "Errors");
	auto* warnings = childWithText<QCheckBox>(dock, "Warnings");
	auto* info = childWithText<QCheckBox>(dock, "Info");
	auto* debug = childWithText<QCheckBox>(dock, "Debug");
	auto* timestamps = childWithText<QCheckBox>(dock, "Timestamps");
	auto* modules = childWithText<QCheckBox>(dock, "Modules");
	auto* autoscroll = childWithText<QCheckBox>(dock, "Autoscroll");
	ASSERT_NE(errors, nullptr);
	ASSERT_NE(warnings, nullptr);
	ASSERT_NE(info, nullptr);
	ASSERT_NE(debug, nullptr);
	ASSERT_NE(timestamps, nullptr);
	ASSERT_NE(modules, nullptr);
	ASSERT_NE(autoscroll, nullptr);
	errors->setChecked(false);
	EXPECT_EQ(view->model()->rowCount(), 1);
	warnings->setChecked(false);
	info->setChecked(false);
	debug->setChecked(false);
	EXPECT_EQ(view->model()->rowCount(), 0);
	info->setChecked(true);
	EXPECT_EQ(view->model()->rowCount(), 1);
	timestamps->setChecked(false);
	modules->setChecked(false);
	EXPECT_TRUE(view->isColumnHidden(LogModel::Timestamp));
	EXPECT_TRUE(view->isColumnHidden(LogModel::Module));

	search->setText("sec*");
	EXPECT_EQ(view->model()->rowCount(), 1);
	auto* regex = childWithText<QToolButton>(dock, ".*");
	ASSERT_NE(regex, nullptr);
	regex->setChecked(true);
	search->setText("^nomatch$");
	EXPECT_EQ(view->model()->rowCount(), 0);

	autoscroll->setChecked(false);
	QLineEdit* scrollback = nullptr;
	for (auto* edit : dock.findChildren<QLineEdit*>())
		if (edit->validator())
			scrollback = edit;
	ASSERT_NE(scrollback, nullptr);
	scrollback->setText("1");
	auto* proxyModel = dynamic_cast<LogProxyModel*>(view->model());
	ASSERT_NE(proxyModel, nullptr);
	auto* sourceModel = dynamic_cast<LogModel*>(proxyModel->sourceModel());
	ASSERT_NE(sourceModel, nullptr);
	EXPECT_EQ(sourceModel->rowCount(), 1);
}

TEST_F(QtWidgetFixture, LogDockSettingsPersistAcrossConstruction)
{
	// Persist to an isolated in-memory-ish settings scope (a unique org/app so the test never touches real settings),
	// cleared up front so the run is deterministic. A fresh LogDock must reopen with the values the prior one left -
	// EVERY control, not just one (the restore-races-its-own-save bug corrupted the store to all-defaults-but-one).
	const QString previousOrg = qApp->organizationName();
	const QString previousApp = qApp->applicationName();
	qApp->setOrganizationName("logerr-test-org");
	qApp->setApplicationName(QString("logdock-persist-%1").arg(std::chrono::steady_clock::now().time_since_epoch().count()));
	QSettings().clear();

	{
		LogDock dock;
		childWithText<QCheckBox>(dock, "Errors")->setChecked(false);
		childWithText<QCheckBox>(dock, "Warnings")->setChecked(false);
		childWithText<QCheckBox>(dock, "Info")->setChecked(false);
		childWithText<QCheckBox>(dock, "Debug")->setChecked(false);
		childWithText<QCheckBox>(dock, "Timestamps")->setChecked(false);
		childWithText<QCheckBox>(dock, "Modules")->setChecked(false);
		childWithText<QCheckBox>(dock, "Autoscroll")->setChecked(false);
	}

	{
		LogDock restored;
		// Every level filter and column toggle the prior dock turned off must come back off - proving the whole set
		// persists, not just the last-changed control.
		EXPECT_FALSE(childWithText<QCheckBox>(restored, "Errors")->isChecked());
		EXPECT_FALSE(childWithText<QCheckBox>(restored, "Warnings")->isChecked());
		EXPECT_FALSE(childWithText<QCheckBox>(restored, "Info")->isChecked());
		EXPECT_FALSE(childWithText<QCheckBox>(restored, "Debug")->isChecked());
		EXPECT_FALSE(childWithText<QCheckBox>(restored, "Timestamps")->isChecked());
		EXPECT_FALSE(childWithText<QCheckBox>(restored, "Modules")->isChecked());
		EXPECT_FALSE(childWithText<QCheckBox>(restored, "Autoscroll")->isChecked());
	}

	QSettings().clear();
	qApp->setOrganizationName(previousOrg);
	qApp->setApplicationName(previousApp);
}

TEST_F(QtWidgetFixture, GuiApplicationHelperFindsTheMainWindow)
{
	QMainWindow mainWindow;
	QWidget otherTopLevel;
	EXPECT_EQ(logerr::getMainWindow(), &mainWindow);
}

#if GTEST_HAS_DEATH_TEST
TEST_F(QtWidgetFixture, FatalQtStackTraceHandlerWritesACrashDumpAndExitsHeadlessly)
{
	const QByteArray inheritedName = qgetenv("LOGERR_QT_TEST_APP_NAME");
	const QString testApplicationName = inheritedName.isNull()
	                                        ? QString("qlogerr-crash-test-%1").arg(std::chrono::steady_clock::now().time_since_epoch().count())
	                                        : QString::fromLocal8Bit(inheritedName);
	if (inheritedName.isNull())
		qputenv("LOGERR_QT_TEST_APP_NAME", testApplicationName.toLocal8Bit());
	const QString previousApplicationName = qApp->applicationName();
	const QByteArray previousSuppress = qgetenv("LOGERR_SUPPRESS_CRASH_DIALOG");
	QStandardPaths::setTestModeEnabled(true);
	qApp->setApplicationName(testApplicationName);
	const auto appDataDirectory = std::filesystem::path(QAPPINFO::appDataDir().toStdString());
	ASSERT_EQ(appDataDirectory.filename().string(), testApplicationName.toStdString());
	std::error_code ignored;
	std::filesystem::remove_all(appDataDirectory, ignored);
	qputenv("LOGERR_SUPPRESS_CRASH_DIALOG", "1");
	EXPECT_EXIT(stackTraceSIGSEGVQt(0), ::testing::ExitedWithCode(1), "");

	const auto crashDirectory = std::filesystem::path(QAPPINFO::crashDumpDir().toStdString());
	ASSERT_TRUE(std::filesystem::is_directory(crashDirectory)) << crashDirectory;
	EXPECT_TRUE(crashDirectory.native().starts_with(appDataDirectory.native())) << crashDirectory;
	EXPECT_FALSE(std::filesystem::is_empty(crashDirectory));
	if (previousSuppress.isNull()) qunsetenv("LOGERR_SUPPRESS_CRASH_DIALOG");
	else qputenv("LOGERR_SUPPRESS_CRASH_DIALOG", previousSuppress);
	if (inheritedName.isNull()) qunsetenv("LOGERR_QT_TEST_APP_NAME");
	qApp->setApplicationName(previousApplicationName);
	QStandardPaths::setTestModeEnabled(false);
	std::filesystem::remove_all(appDataDirectory, ignored);
}
#endif

TEST_F(QtWidgetFixture, ExceptionDialogConstructorsButtonsClipboardAndSizingWorkOffscreen)
{
	StackTraceException stackError("boom", "file.cpp", "function", 9, false);
	ExceptionDialog dialog(stackError, false);
	EXPECT_EQ(dialog.windowTitle(), "ERROR");
	auto* details = childWithText<QPushButton>(dialog, "Show Details");
	auto* copy = childWithText<QPushButton>(dialog, "Copy Error");
	auto* ok = childWithText<QPushButton>(dialog, "OK");
	ASSERT_NE(details, nullptr);
	ASSERT_NE(copy, nullptr);
	ASSERT_NE(ok, nullptr);
	details->click();
	copy->click();
	EXPECT_TRUE(QApplication::clipboard()->text().contains("boom"));
	for (const QString text : {"App Info", "Version Info", "Build Info", "Host Info", "Stack Trace"})
	{
		auto* button = childWithText<QPushButton>(dialog, text);
		ASSERT_NE(button, nullptr);
		button->click();
	}
	ok->click();
	EXPECT_FALSE(dialog.isVisible());

	const std::runtime_error standard("standard details");
	ExceptionDialog standardDialog(standard, true);
	EXPECT_EQ(standardDialog.windowTitle(), "FATAL ERROR");
	ExceptionDialog textDialog("text details", false);
	EXPECT_EQ(textDialog.windowTitle(), "ERROR");
	ExceptionDialog customDialog(QStringLiteral("custom"), QStringLiteral("details"), true);
	EXPECT_EQ(customDialog.windowTitle(), "FATAL ERROR");

	CorrectlySizedTextBrowser browser;
	browser.setPlainText(QStringLiteral("a reasonably wide line"));
	EXPECT_EQ(browser.minimumSizeHint(), browser.sizeHint());
	EXPECT_GT(browser.sizeHint().width(), 30);
}

TEST_F(QtWidgetFixture, ExceptionDialogMessageWrapsAndDialogStaysOnScreenHorizontally)
{
	// A single long line with no spaces to break on - the worst case for the pre-#652 unwrapped label, which grew the
	// fixed-size dialog wider than the display (the off-screen bug on a small/RDP screen).
	const QString longMessage = QStringLiteral(
		"An unrecoverable configuration error occurred while loading the manifest and Helen could not build the fleet. ")
		+ QString(600, QLatin1Char('x'));
	ExceptionDialog dialog(longMessage, QStringLiteral("details"), false);

	// The summary label word-wraps and is width-capped so it never stretches the dialog past a screen-relative measure.
	QLabel* message = nullptr;
	for (auto* label : dialog.findChildren<QLabel*>())
		if (label->text().startsWith("FATAL ERROR: ") || label->text().startsWith("ERROR: "))
		{
			message = label;
			break;
		}
	ASSERT_NE(message, nullptr);
	EXPECT_TRUE(message->wordWrap());
	EXPECT_GT(message->maximumWidth(), 0);
	EXPECT_LT(message->maximumWidth(), QWIDGETSIZE_MAX);

	// After showing, the dialog frame sits within the screen HORIZONTALLY (the showEvent clamp), so the message is
	// readable rather than clipped off the right edge. The vertical position is not asserted (the fix is horizontal-only).
	dialog.show();
	ASSERT_TRUE(QTest::qWaitForWindowExposed(&dialog));
	const QScreen* screen = dialog.screen() ? dialog.screen() : QGuiApplication::primaryScreen();
	ASSERT_NE(screen, nullptr);
	const QRect avail = screen->availableGeometry();
	const QRect frame = dialog.frameGeometry();
	EXPECT_LE(frame.left(), avail.right());       // the dialog is not pushed entirely off the right edge
	EXPECT_GE(frame.left(), avail.left());        // and its left edge is on-screen
	dialog.close();
}

TEST_F(LogerrTest, LogReceiverEmitsEveryPendingLocalDatagram)
{
	QUdpSocket reservation;
	ASSERT_TRUE(reservation.bind(QHostAddress::LocalHost, 0));
	const quint16 port = reservation.localPort();
	reservation.close();

	const QByteArray oldPort = qgetenv("LOGERR_LOG_PORT");
	const QByteArray oldGroup = qgetenv("LOGERR_LOG_GROUP");
	qputenv("LOGERR_LOG_PORT", QByteArray::number(port));
	qputenv("LOGERR_LOG_GROUP", "239.1.2.3");
	LogReceiver receiver;
	QSignalSpy received(&receiver, &LogReceiver::readyRead);
	QUdpSocket sender;
	EXPECT_EQ(sender.writeDatagram("one", QHostAddress::LocalHost, port), 3);
	EXPECT_EQ(sender.writeDatagram("two", QHostAddress::LocalHost, port), 3);
	ASSERT_TRUE(received.wait(2s));
	ASSERT_TRUE(runUntil([&] { return received.count() == 2; }));
	EXPECT_EQ(received.at(0).at(0).value<std::string>(), "one");
	EXPECT_EQ(received.at(1).at(0).value<std::string>(), "two");

	if (oldPort.isNull())
		qunsetenv("LOGERR_LOG_PORT");
	else
		qputenv("LOGERR_LOG_PORT", oldPort);
	if (oldGroup.isNull())
		qunsetenv("LOGERR_LOG_GROUP");
	else
		qputenv("LOGERR_LOG_GROUP", oldGroup);
}

TEST_F(LogerrTest, QCoreAppThreadIsANoOpWhenTheApplicationAlreadyExists)
{
	EXPECT_EQ(QCoreAppThread::instance(), nullptr);
}

TEST_F(QtWidgetFixture, ApplicationNotifyReportsNonFatalErrorsAndPropagatesFatalOnes)
{
	auto* application = qobject_cast<Application*>(QApplication::instance());
	ASSERT_NE(application, nullptr);
	QEvent event(QEvent::User);
	ThrowingEventTarget accepted(ThrowingEventTarget::Behavior::Accept);
	EXPECT_TRUE(application->notify(&accepted, &event));

	ThrowingEventTarget nonFatal(ThrowingEventTarget::Behavior::NonFatalLogerr);
	QTimer::singleShot(0, &closeActiveModalWidget);
	EXPECT_FALSE(application->notify(&nonFatal, &event));

	ThrowingEventTarget fatal(ThrowingEventTarget::Behavior::FatalLogerr);
	QTimer::singleShot(0, &closeActiveModalWidget);
	EXPECT_THROW(application->notify(&fatal, &event), logerr::exception);

	ThrowingEventTarget standard(ThrowingEventTarget::Behavior::StandardException);
	QTimer::singleShot(0, &closeActiveModalWidget);
	EXPECT_THROW(application->notify(&standard, &event), std::runtime_error);

	ThrowingEventTarget unknown(ThrowingEventTarget::Behavior::UnknownException);
	QTimer::singleShot(0, &closeActiveModalWidget);
	EXPECT_ANY_THROW(application->notify(&unknown, &event));
}

TEST_F(QtWidgetFixture, ApplicationNotifyConsumesPendingWorkerExceptions)
{
	auto* application = qobject_cast<Application*>(QApplication::instance());
	ASSERT_NE(application, nullptr);
	QEvent event(QEvent::User);
	ThrowingEventTarget accepted(ThrowingEventTarget::Behavior::Accept);
	logerr::captureException(std::make_exception_ptr(std::runtime_error("pending GUI worker failure")));
	QTimer::singleShot(0, &closeActiveModalWidget);
	EXPECT_THROW(application->notify(&accepted, &event), std::runtime_error);
	EXPECT_EQ(logerr::takeException(), nullptr);
}

int main(int argc, char** argv)
{
	if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
		qputenv("QT_QPA_PLATFORM", "offscreen");
	Application app(argc, argv);
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
