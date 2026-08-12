#define _CONCURRENT_QUEUE_NO_WARNINGS

#include <LogFileWriter.h>
#include <LogStream.h>
#include <StackTrace.h>
#include <StackTraceException.h>
#include <StackTraceSIGSEGV.h>
#include <appinfo.h>
#include <concurrent_queue.h>
#include <function_view.h>
#include <logerrThread.h>
#include <sigtermHandler.h>
#include <timestampLite.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <barrier>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

namespace
{
	class LogerrCoreFixture : public ::testing::Test
	{
	protected:
		void SetUp() override { static_cast<void>(logerr::takeException()); }
		void TearDown() override { static_cast<void>(logerr::takeException()); }

		static std::filesystem::path uniquePath(std::string_view suffix)
		{
			return std::filesystem::temp_directory_path() /
			       ("logerr-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
			        std::string(suffix));
		}
	};

	class ScopedEnvironment
	{
	public:
		ScopedEnvironment(std::string name, const std::string& value) : m_name(std::move(name))
		{
			if (const char* previous = std::getenv(m_name.c_str()))
				m_previous = previous;
			set(value);
		}

		~ScopedEnvironment()
		{
			if (m_previous)
				set(*m_previous);
			else
				unset();
		}

		ScopedEnvironment(const ScopedEnvironment&) = delete;
		ScopedEnvironment& operator=(const ScopedEnvironment&) = delete;

	private:
		void set(const std::string& value) const
		{
#ifdef _WIN32
			_putenv_s(m_name.c_str(), value.c_str());
#else
			setenv(m_name.c_str(), value.c_str(), 1);
#endif
		}

		void unset() const
		{
#ifdef _WIN32
			_putenv_s(m_name.c_str(), "");
#else
			unsetenv(m_name.c_str());
#endif
		}

		std::string                m_name;
		std::optional<std::string> m_previous;
	};

	struct NonDefault
	{
		explicit NonDefault(int value) : value(value) {}
		int value;
	};

	class LogStreamProbe : public LogStream
	{
	public:
		using LogStream::LogStream;
		using LogStream::overflow;
		using LogStream::xsputn;
	};
}

TEST_F(LogerrCoreFixture, ConcurrentQueueConstructorsAssignmentsAndOrdering)
{
	const std::vector<int> values{1, 2, 3};
	concurrent_queue<int> empty(std::allocator<int>{});
	concurrent_queue<int> sized(3);
	concurrent_queue<int> filled(3, 7);
	concurrent_queue<int> ranged(values.begin(), values.end());
	concurrent_queue<int> listed{1, 2, 3};

	EXPECT_TRUE(empty.empty());
	EXPECT_EQ(sized.size(), 3U);
	EXPECT_EQ(filled.size(), 3U);
	EXPECT_EQ(ranged, listed);
	EXPECT_EQ(ranged.get_allocator(), std::allocator<int>{});
	EXPECT_EQ(ranged <=> filled, std::strong_ordering::less);
	EXPECT_EQ(ranged <=> ranged, std::strong_ordering::equal);

	concurrent_queue<int> copied(ranged);
	concurrent_queue<int> copiedWithAllocator(ranged, std::allocator<int>{});
	EXPECT_EQ(copied, ranged);
	EXPECT_EQ(copiedWithAllocator, ranged);

	concurrent_queue<int> moved(std::move(copied));
	concurrent_queue<int> movedWithAllocator(std::move(copiedWithAllocator), std::allocator<int>{});
	EXPECT_EQ(moved, ranged);
	EXPECT_EQ(movedWithAllocator, ranged);

	empty = ranged;
	EXPECT_EQ(empty, ranged);
	empty = empty;
	EXPECT_EQ(empty, ranged);

	concurrent_queue<int> moveAssigned;
	moveAssigned = std::move(moved);
	EXPECT_EQ(moveAssigned, ranged);
	EXPECT_NE(moveAssigned, filled);

	swap(moveAssigned, filled);
	EXPECT_EQ(moveAssigned.size(), 3U);
	EXPECT_EQ(filled, ranged);
	swap(filled, filled);
	EXPECT_EQ(filled, ranged);
}

TEST_F(LogerrCoreFixture, ConcurrentQueueDebugIterationAndClear)
{
	concurrent_queue<int> queue{1, 2, 3};
	{
		auto lock = queue.acquire_read_lock();
		EXPECT_EQ(std::vector<int>(queue.begin(), queue.end()), (std::vector<int>{1, 2, 3}));
		const auto& constQueue = queue;
		EXPECT_EQ(std::vector<int>(constQueue.begin(), constQueue.end()), (std::vector<int>{1, 2, 3}));
		EXPECT_EQ(std::vector<int>(constQueue.cbegin(), constQueue.cend()), (std::vector<int>{1, 2, 3}));
	}
	{
		auto lock = queue.acquire_write_lock();
		*queue.begin() = 9;
	}
	EXPECT_EQ(queue.try_pop().value(), 9);
	queue.clear();
	EXPECT_TRUE(queue.empty());
}

TEST_F(LogerrCoreFixture, ConcurrentQueuePopTimeoutStopAndMoveOnlyPaths)
{
	concurrent_queue<NonDefault> nonDefault;
	EXPECT_FALSE(nonDefault.try_pop().has_value());
	nonDefault.emplace(11);
	auto value = nonDefault.try_pop();
	ASSERT_TRUE(value.has_value());
	EXPECT_EQ(value->value, 11);

	concurrent_queue<std::unique_ptr<int>> moveOnly;
	moveOnly.push(std::make_unique<int>(12));
	auto pointer = moveOnly.try_pop();
	ASSERT_TRUE(pointer.has_value());
	ASSERT_NE(*pointer, nullptr);
	EXPECT_EQ(**pointer, 12);

	concurrent_queue<int> timed;
	int destination = -1;
	EXPECT_FALSE(timed.try_pop(destination));
	EXPECT_FALSE(timed.try_pop_for(destination, 2ms));
	std::jthread producer([&] {
		std::this_thread::sleep_for(10ms);
		timed.push(42);
	});
	EXPECT_TRUE(timed.try_pop_for(destination, 1s));
	EXPECT_EQ(destination, 42);

	std::stop_source stopped;
	stopped.request_stop();
	timed.push(7);
	EXPECT_TRUE(timed.wait_pop(destination, stopped.get_token()));
	EXPECT_EQ(destination, 7);
	EXPECT_FALSE(timed.wait_pop(destination, stopped.get_token()));
}

TEST_F(LogerrCoreFixture, ConcurrentQueueNonBlockingPopDoesNotWaitForAContendedLock)
{
	concurrent_queue<int> queue{1};
	std::promise<void> locked;
	std::promise<void> release;
	auto releaseFuture = release.get_future();
	std::jthread holder([&] {
		auto lock = queue.acquire_write_lock();
		locked.set_value();
		releaseFuture.wait();
	});
	locked.get_future().wait();

	int value = 0;
	EXPECT_FALSE(queue.try_pop(value));
	auto optional = queue.try_pop();
	EXPECT_FALSE(optional.has_value());
	auto timed = std::async(std::launch::async, [&] { return queue.try_pop_for(value, 5ms); });
	EXPECT_FALSE(timed.get());
	release.set_value();
}

TEST_F(LogerrCoreFixture, LogStreamDispatchesFlushesAndRestoresTheOriginalBuffer)
{
	std::ostringstream stream;
	std::vector<std::string> first;
	std::vector<std::string> second;
	{
		LogStreamProbe logger(stream);
		logger.registerLogFunction("first", [&](std::string text) { first.push_back(std::move(text)); });
		logger.registerLogFunction("second", [&](std::string text) { second.push_back(std::move(text)); });
		logger.registerLogFunction("first", [](std::string) { FAIL() << "duplicate names must not replace callbacks"; });

		EXPECT_EQ(logger.overflow(std::char_traits<char>::eof()), std::char_traits<char>::not_eof(std::char_traits<char>::eof()));
		EXPECT_EQ(logger.xsputn("", 0), 0);
		stream << "one" << '\n';
		logger.unregisterLogFunction("missing");
		logger.unregisterLogFunction("second");
		stream << "two\nthree";
	}

	EXPECT_EQ(first, (std::vector<std::string>{"one\n", "two\nthree"}));
	EXPECT_EQ(second, (std::vector<std::string>{"one\n"}));
	stream << "restored";
	EXPECT_EQ(stream.str(), "restored");
}

TEST_F(LogerrCoreFixture, LogStreamCanRemoveEveryCallback)
{
	std::ostringstream stream;
	int calls = 0;
	{
		LogStream logger(stream);
		logger.registerLogFunction("callback", [&](std::string) { ++calls; });
		logger.unregisterLogFunction();
		stream << "discarded\n";
	}
	EXPECT_EQ(calls, 0);
}

TEST_F(LogerrCoreFixture, LogStreamDestructorContainsCallbackFailuresAndRestoresTheStream)
{
	std::ostringstream stream;
	EXPECT_NO_THROW({
		LogStream logger(stream);
		logger.registerLogFunction("throwing", [](const std::string&) { throw std::runtime_error("callback failure"); });
		stream << "unterminated";
	});
	stream << "restored";
	EXPECT_EQ(stream.str(), "restored");
}

TEST_F(LogerrCoreFixture, LogFileWriterDrainsConcurrentProducersBeforeDestruction)
{
	const auto path = uniquePath(".log");
	constexpr int producerCount = 4;
	constexpr int linesPerProducer = 100;
	{
		LogFileWriter writer(path.string());
		std::vector<std::jthread> producers;
		for (int producer = 0; producer < producerCount; ++producer)
		{
			producers.emplace_back([&, producer] {
				for (int line = 0; line < linesPerProducer; ++line)
					writer.write(std::to_string(producer) + ':' + std::to_string(line) + '\n');
			});
		}
	}

	std::ifstream input(path);
	std::vector<std::string> lines;
	for (std::string line; std::getline(input, line);)
		lines.push_back(std::move(line));
	EXPECT_EQ(lines.size(), static_cast<size_t>(producerCount * linesPerProducer));
	std::error_code ignored;
	std::filesystem::remove(path, ignored);
}

TEST_F(LogerrCoreFixture, LogFileWriterReportsAnUnopenableDestinationWithoutThrowing)
{
	const auto directory = uniquePath("-directory");
	ASSERT_TRUE(std::filesystem::create_directory(directory));
	std::ostringstream errors;
	auto* oldBuffer = std::cerr.rdbuf(errors.rdbuf());
	{
		LogFileWriter writer(directory.string());
		writer.write("ignored");
	}
	std::cerr.rdbuf(oldBuffer);
	EXPECT_NE(errors.str().find("Failed to open the log file"), std::string::npos);
	std::error_code ignored;
	std::filesystem::remove(directory, ignored);
}

TEST_F(LogerrCoreFixture, LogFileWriterCreatesItsDefaultDirectoryAndFile)
{
	const auto sandbox = uniquePath("-home");
	ASSERT_TRUE(std::filesystem::create_directories(sandbox));
#ifdef _WIN32
	ScopedEnvironment appData("APPDATA", sandbox.string());
#else
	ScopedEnvironment home("HOME", sandbox.string());
#endif
	const auto logDirectory = std::filesystem::path(APPINFO::logDir());
	ASSERT_FALSE(std::filesystem::exists(logDirectory));
	{
		LogFileWriter writer;
		writer.write("default-path-entry\n");
	}

	std::vector<std::filesystem::path> files;
	for (const auto& entry : std::filesystem::directory_iterator(logDirectory))
		if (entry.is_regular_file())
			files.push_back(entry.path());
	ASSERT_EQ(files.size(), 1u);
	std::ifstream input(files.front());
	EXPECT_EQ(std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()), "default-path-entry\n");
	std::error_code ignored;
	std::filesystem::remove_all(sandbox, ignored);
}

#if GTEST_HAS_DEATH_TEST
TEST_F(LogerrCoreFixture, FatalStackTraceHandlerWritesACrashDumpAndExits)
{
	const char* inheritedSandbox = std::getenv("LOGERR_TEST_CRASH_SANDBOX");
	const auto sandbox = inheritedSandbox ? std::filesystem::path(inheritedSandbox) : uniquePath("-crash-home");
	ASSERT_TRUE(std::filesystem::create_directories(sandbox) || std::filesystem::is_directory(sandbox));
	std::optional<ScopedEnvironment> sandboxIdentity;
	if (!inheritedSandbox)
		sandboxIdentity.emplace("LOGERR_TEST_CRASH_SANDBOX", sandbox.string());
#ifdef _WIN32
	ScopedEnvironment appData("APPDATA", sandbox.string());
#else
	ScopedEnvironment home("HOME", sandbox.string());
#endif
	EXPECT_EXIT(stackTraceSIGSEGV(0), ::testing::ExitedWithCode(1), "");

	const auto crashDirectory = std::filesystem::path(APPINFO::crashDumpDir());
	ASSERT_TRUE(std::filesystem::is_directory(crashDirectory)) << crashDirectory;
	EXPECT_FALSE(std::filesystem::is_empty(crashDirectory));
	std::error_code ignored;
	std::filesystem::remove_all(sandbox, ignored);
}
#endif

TEST_F(LogerrCoreFixture, LogerrThreadSupportsStopTokensArgumentsAndDefaultConstruction)
{
	logerr::thread empty;
	EXPECT_FALSE(empty.joinable());

	std::promise<int> plainResult;
	{
		logerr::thread plain([&](int left, int right) { plainResult.set_value(left + right); }, 20, 22);
	}
	EXPECT_EQ(plainResult.get_future().get(), 42);

	std::atomic_bool observedStop{false};
	{
		logerr::thread stoppable(
		    [&](std::stop_token stop, int expected) {
			    while (!stop.stop_requested())
				    std::this_thread::yield();
			    observedStop.store(expected == 7);
		    },
		    7);
	}
	EXPECT_TRUE(observedStop.load());
}

TEST_F(LogerrCoreFixture, ExceptionHandoffKeepsTheFirstFailureAndConsumesItOnce)
{
	logerr::captureException(nullptr);
	EXPECT_EQ(logerr::takeException(), nullptr);
	logerr::captureException(std::make_exception_ptr(std::runtime_error("first")));
	logerr::captureException(std::make_exception_ptr(std::runtime_error("second")));
	auto failure = logerr::takeException();
	ASSERT_NE(failure, nullptr);
	EXPECT_EQ(logerr::takeException(), nullptr);
	try
	{
		std::rethrow_exception(failure);
	}
	catch (const std::runtime_error& error)
	{
		EXPECT_STREQ(error.what(), "first");
	}

	{
		logerr::thread worker([] { throw 17; });
	}
	EXPECT_NE(logerr::takeException(), nullptr);
}

TEST_F(LogerrCoreFixture, StackTraceExceptionExposesStableMetadata)
{
	StackTraceException error("message", "source.cpp", "function()", 123, true);
	EXPECT_EQ(error.errorMessage(), "message");
	EXPECT_EQ(error.filename(), "source.cpp");
	EXPECT_EQ(error.function(), "function()");
	EXPECT_EQ(error.line(), 123U);
	EXPECT_TRUE(error.fatal());
	EXPECT_TRUE(std::string(error.what()).starts_with("FATAL message"));
	EXPECT_EQ(error.errorDetails(), error.what());
	EXPECT_NO_THROW(static_cast<void>(error.trace()));
}

TEST_F(LogerrCoreFixture, StackTraceCanBeCapturedConcurrently)
{
	constexpr int workerCount = 4;
	std::barrier start(workerCount);
	std::atomic_int completed{0};
	{
		std::vector<std::jthread> workers;
		workers.reserve(workerCount);
		for (int worker = 0; worker < workerCount; ++worker)
		{
			workers.emplace_back([&] {
				start.arrive_and_wait();
				for (int iteration = 0; iteration < 2; ++iteration)
					static_cast<void>(StackTrace{}.data());
				++completed;
			});
		}
	}
	EXPECT_EQ(completed.load(), workerCount);
}

TEST_F(LogerrCoreFixture, TimestampAndSourceFilenameArePortable)
{
	TimestampLite timestamp;
	const auto point = static_cast<std::chrono::system_clock::time_point>(timestamp);
	EXPECT_EQ(std::chrono::system_clock::to_time_t(point), static_cast<std::time_t>(timestamp));
	std::ostringstream stream;
	stream << timestamp;
	EXPECT_EQ(stream.str(), static_cast<std::string>(timestamp));
	EXPECT_EQ(logerr::sourceFilename("/a/b/source.cpp"), std::string_view("source.cpp"));
	EXPECT_EQ(logerr::sourceFilename("C:\\a\\b\\source.cpp"), std::string_view("source.cpp"));
	EXPECT_EQ(logerr::sourceFilename("source.cpp"), std::string_view("source.cpp"));
}

TEST_F(LogerrCoreFixture, ErrorLoggingAddsSourceContextAndOptionalTrace)
{
	std::ostringstream captured;
	auto* const originalBuffer = std::cout.rdbuf(captured.rdbuf());

	const int errorLine = __LINE__ + 1;
	LOGERR << "ordinary diagnostic" << ENDL;
	const int warningLine = __LINE__ + 1;
	LOGWARNING << "warning diagnostic" << ENDL;
	const int traceLine = __LINE__ + 1;
	LOGERR_TRACE("traced diagnostic");

	std::cout.rdbuf(originalBuffer);
	const std::string output = captured.str();
	EXPECT_NE(output.find("[ERROR]"), std::string::npos);
	EXPECT_NE(output.find("[WARNING]"), std::string::npos);
	EXPECT_NE(output.find("test_logerr.cpp:" + std::to_string(errorLine)), std::string::npos);
	EXPECT_NE(output.find("test_logerr.cpp:" + std::to_string(warningLine)), std::string::npos);
	EXPECT_NE(output.find("test_logerr.cpp:" + std::to_string(traceLine)), std::string::npos);
	EXPECT_NE(output.find("ErrorLoggingAddsSourceContextAndOptionalTrace"), std::string::npos);
	EXPECT_NE(output.find("ordinary diagnostic"), std::string::npos);
	EXPECT_NE(output.find("warning diagnostic"), std::string::npos);
	EXPECT_NE(output.find("traced diagnostic\n"), std::string::npos);
	EXPECT_NE(output.find("0x"), std::string::npos);
}

TEST_F(LogerrCoreFixture, TimestampFormattingIsSafeUnderConcurrency)
{
	constexpr int workerCount = 8;
	constexpr int iterationsPerWorker = 250;
	std::atomic_int failures{0};
	{
		std::vector<std::jthread> workers;
		workers.reserve(workerCount);
		for (int worker = 0; worker < workerCount; ++worker)
		{
			workers.emplace_back([&] {
				for (int iteration = 0; iteration < iterationsPerWorker; ++iteration)
				{
					const auto timestamp = static_cast<std::string>(TimestampLite{});
					if (timestamp.empty() || !timestamp.contains('.'))
						++failures;
				}
			});
		}
	}
	EXPECT_EQ(failures.load(), 0);
}

TEST_F(LogerrCoreFixture, SigtermHandlerUsesTheLibraryTerminationType)
{
	EXPECT_THROW(sigtermHandler(0), TerminateException);
}

TEST_F(LogerrCoreFixture, FunctionViewInvokesAReferencedCallable)
{
	int total = 0;
	auto add = [&](int value) { total += value; return total; };
	function_view<int(int)> view(add);
	EXPECT_EQ(view(20), 20);
	EXPECT_EQ(view(22), 42);
}

TEST_F(LogerrCoreFixture, AppInfoAccessorsAreCallableAndSystemDetailsAreStructured)
{
	const std::vector<std::string> values{
	    APPINFO::name(), APPINFO::organization(), APPINFO::organizationDomain(), APPINFO::version(),
	    APPINFO::gitBranch(), APPINFO::gitCommitShort(), APPINFO::gitCommitLong(), APPINFO::gitTag(),
	    APPINFO::gitDirty(), APPINFO::gitOrigin(), APPINFO::gitDirectory(), APPINFO::gitRepo(), APPINFO::gitUser(),
	    APPINFO::gitEmail(), APPINFO::buildHostname(), APPINFO::buildOSName(), APPINFO::buildOSVersion(),
	    APPINFO::buildOSProcessor(), APPINFO::cmakeVersion(), APPINFO::compilerName(), APPINFO::compilerVersion(),
	    APPINFO::qtVersion(), APPINFO::hostCPUArchitecture(), APPINFO::hostKernelType(), APPINFO::hostKernelVersion(),
	    APPINFO::hostName(), APPINFO::hostUniqueID(), APPINFO::hostPrettyProductName(), APPINFO::hostProductType(),
	    APPINFO::hostProductVersion(), APPINFO::home(), APPINFO::appDataDir(), APPINFO::logDir(), APPINFO::crashDumpDir(),
	    APPINFO::documentsDir(), APPINFO::tempDir(), APPINFO::configDir(), APPINFO::applicationStartTime()};
	EXPECT_EQ(values.size(), 38U);
	EXPECT_FALSE(APPINFO::name().empty());
	EXPECT_NE(APPINFO::systemDetails().find("APPLICATION INFO:"), std::string::npos);
}
