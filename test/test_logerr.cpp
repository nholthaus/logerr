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

//----------------------------------------------------------------------------------------------------------------------
// A captured trace must actually RESOLVE symbols, not degrade to the "<unable to initialize Windows symbols>" placeholder.
// A large host process (a Qt GUI with 100+ loaded modules, an embedded interpreter) made the old eager whole-process
// SymInitialize(fInvadeProcess=TRUE) return FALSE; that failure was cached in a process-lifetime static, so every later
// trace printed the placeholder and the trace footer carried no frames - the diagnostic value was lost. This test is the
// guard: a fresh trace resolves to real content, never the placeholder. On a build with symbol info it also names the
// enclosing frame; a stripped build (no PDB / no BFD symbols) still resolves module+offset, so the frame-name assertion
// is gated on symbol info being present rather than asserted unconditionally.
#if defined(_MSC_VER)
#define LOGERR_TEST_NOINLINE __declspec(noinline)
#else
#define LOGERR_TEST_NOINLINE __attribute__((noinline))
#endif
// Kept out of line (never inlined) so the innermost frame is a real, named function the walker can resolve.
LOGERR_TEST_NOINLINE static std::string traceProbeInnermost()
{
	return static_cast<std::string>(StackTrace{});
}

TEST_F(LogerrCoreFixture, StackTraceResolvesSymbolsAndIsNotThePlaceholder)
{
	const std::string trace = traceProbeInnermost();
	// The placeholder is the exact failure the fInvadeProcess=FALSE + deferred-loads + refresh fix eliminates.
	EXPECT_EQ(trace.find("<unable to initialize Windows symbols>"), std::string::npos) << trace;
	EXPECT_FALSE(trace.empty());
	// Every resolved frame the formatter emits carries an address column; a non-empty trace has at least one.
	EXPECT_NE(trace.find("0x"), std::string::npos) << trace;
	// When symbols are available, the innermost probe frame is named. Detect symbol availability by whether ANY frame
	// resolved to a real name (i.e. not every frame is "<no symbol found>"); only then assert the probe name is present.
	const bool anyResolved = trace.find("<no symbol found>") == std::string::npos
	                         || trace.find("traceProbeInnermost") != std::string::npos;
	if (anyResolved)
	{
		EXPECT_NE(trace.find("traceProbeInnermost"), std::string::npos) << trace;
	}
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

// --- LOGERR always logs a stack trace, deduped per call site (logerr 1.2.2) -------------------------------------------
// Capture std::cout for the duration of a block, restoring the original buffer on scope exit (RAII, exception-safe).
namespace
{
	class CoutCapture
	{
	public:
		CoutCapture() : m_previous(std::cout.rdbuf(m_captured.rdbuf())) {}
		~CoutCapture() { std::cout.rdbuf(m_previous); }
		CoutCapture(const CoutCapture&)            = delete;
		CoutCapture& operator=(const CoutCapture&) = delete;
		std::string str() const { return m_captured.str(); }
	private:
		std::ostringstream m_captured;
		std::streambuf*    m_previous;
	};

	// Count how many stack-trace footers a captured LOGERR output contains. Each footer opens with the "\n" the
	// destructor writes before the trace; the trace lines carry hex frame addresses ("0x"). Counting the "0x"-bearing
	// blocks is brittle, so instead count trace footers by the marker the StackTrace formatter always emits per frame.
	// A trace is present iff the output contains a hex address; distinct traces are separated by a fresh prefix line.
	std::size_t occurrences(const std::string& haystack, const std::string& needle)
	{
		std::size_t count = 0;
		for (std::size_t pos = haystack.find(needle); pos != std::string::npos; pos = haystack.find(needle, pos + needle.size()))
			++count;
		return count;
	}

	// One LOGERR line reached from two different call paths. Kept out of line so pathAlpha/pathBravo are real, distinct
	// enclosing frames in the captured stack; the LOGERR itself is a single source line, so line-based deduplication
	// WOULD have collapsed the second path - stack-based deduplication must not.
	volatile int g_pathSink = 0;    ///< Defeats tail-call optimization so pathAlpha/pathBravo remain real return frames.
	LOGERR_TEST_NOINLINE void sharedLogSite()
	{
		LOGERR << "shared-site failure" << ENDL;
	}
	LOGERR_TEST_NOINLINE void pathAlpha() { sharedLogSite(); g_pathSink += 1; }
	LOGERR_TEST_NOINLINE void pathBravo() { sharedLogSite(); g_pathSink += 2; }

	// Capture a deduplicated trace from ONE fixed call site (this function's body). Two calls to it from the same caller
	// line produce the identical stack, so the second is suppressed - the vehicle for the single-stack deduplication and
	// thread-safety assertions. Out of line so it is a real, stable frame.
	LOGERR_TEST_NOINLINE StackTrace deduplicatedTraceFromSharedStack()
	{
		return StackTrace(0, /*deduplicateByStack*/ true);
	}
}

TEST_F(LogerrCoreFixture, LogErrAlwaysCarriesAStackTraceOnAFreshStack)
{
	logerr::resetTracedSites();
	CoutCapture capture;

	LOGERR << "fresh failure" << ENDL;

	const std::string output = capture.str();
	EXPECT_NE(output.find("[ERROR]"), std::string::npos);
	EXPECT_NE(output.find("fresh failure"), std::string::npos);
	// The whole-trace footer: at least one hex frame address follows the message on a first-seen stack.
	EXPECT_NE(output.find("0x"), std::string::npos) << "a fresh stack must carry the full stack trace";
}

TEST_F(LogerrCoreFixture, LogErrDeduplicatesTheTracePerIdenticalStack)
{
	logerr::resetTracedSites();
	CoutCapture capture;

	// The SAME LOGERR line hit three times in a loop is the SAME stack each time (identical return addresses): the
	// trace footer must appear exactly ONCE (first hit), then message-only on the repeats.
	for (int i = 0; i < 3; ++i)
		LOGERR << "recurring failure " << i << ENDL;

	const std::string output = capture.str();
	EXPECT_EQ(occurrences(output, "recurring failure "), 3U) << "every hit logs its message line";
	// Exactly one trace footer for three identical-stack repeats.
	const std::size_t secondMsg = output.find("recurring failure 1");
	const std::size_t thirdMsg  = output.find("recurring failure 2");
	ASSERT_NE(output.find("recurring failure 0"), std::string::npos);
	ASSERT_NE(secondMsg, std::string::npos);
	ASSERT_NE(thirdMsg, std::string::npos);
	// Between the 2nd and 3rd message there is no trace footer (an identical-stack repeat is message-only).
	const std::size_t traceAfterSecond = output.find("0x", secondMsg + 1);
	EXPECT_TRUE(traceAfterSecond == std::string::npos || traceAfterSecond > thirdMsg)
	    << "a repeated identical stack must not re-emit the stack trace";
}

TEST_F(LogerrCoreFixture, DifferentCallPathsThroughTheSameLineEachTrace)
{
	// The core reason deduplication keys on the STACK, not the source line: a single LOGERR line reached by two
	// genuinely different call paths must NOT collapse - each distinct stack is diagnostically important and gets its
	// own full trace. Two helper functions both call sharedLogSite(); the LOGERR inside sharedLogSite is one line, but
	// the enclosing return address (pathAlpha vs pathBravo) differs, so the stacks differ and both must trace.
	logerr::resetTracedSites();
	CoutCapture capture;

	pathAlpha();   // -> sharedLogSite() -> LOGERR
	pathBravo();   // -> sharedLogSite() -> LOGERR  (same line, different stack)

	const std::string output = capture.str();
	EXPECT_EQ(occurrences(output, "shared-site failure"), 2U) << "both paths log the message";
	// Both distinct stacks trace: a trace footer (a hex frame) appears after the first message AND after the second.
	const std::size_t firstMsg  = output.find("shared-site failure");
	ASSERT_NE(firstMsg, std::string::npos);
	const std::size_t secondMsg = output.find("shared-site failure", firstMsg + 1);
	ASSERT_NE(secondMsg, std::string::npos);
	const std::size_t traceAfterFirst = output.find("0x", firstMsg);
	EXPECT_NE(traceAfterFirst, std::string::npos);
	EXPECT_LT(traceAfterFirst, secondMsg) << "the first call path traces before the second path's message";
	EXPECT_NE(output.find("0x", secondMsg), std::string::npos)
	    << "the second, DISTINCT call path through the same line must ALSO carry its own trace";
	// The two traces name their distinct enclosing frames, proving the stacks really differ.
	EXPECT_NE(output.find("pathAlpha"), std::string::npos);
	EXPECT_NE(output.find("pathBravo"), std::string::npos);
}

TEST_F(LogerrCoreFixture, ResetTracedSitesReArmsTheTrace)
{
	// Hit ONE stack twice (first traces, repeat deduplicated), then reset, then hit it again: after the reset the
	// same stack must trace anew.
	logerr::resetTracedSites();
	std::string beforeReset;
	std::string afterReset;
	{
		CoutCapture capture;
		for (int i = 0; i < 2; ++i)
			LOGERR << "reset probe" << ENDL;   // same stack: traces once, then deduplicated
		beforeReset = capture.str();
	}
	logerr::resetTracedSites();
	{
		CoutCapture capture;
		LOGERR << "reset probe again" << ENDL;   // after the reset the registry is empty: must trace
		afterReset = capture.str();
	}
	EXPECT_NE(beforeReset.find("0x"), std::string::npos) << "the first hit traced";
	EXPECT_NE(afterReset.find("0x"), std::string::npos) << "after resetTracedSites, a stack traces again";
}

TEST_F(LogerrCoreFixture, LogErrPreservesTheStreamedMessageAndManipulators)
{
	logerr::resetTracedSites();
	CoutCapture capture;

	// Mixed streamed types + std::endl + ENDL must all compile and land in the message verbatim.
	LOGERR << "count=" << 42 << " ratio=" << 3.5 << " flag=" << true << std::endl;
	LOGERR << "second" << ENDL;

	const std::string output = capture.str();
	EXPECT_NE(output.find("count=42 ratio=3.5 flag=1"), std::string::npos);
	EXPECT_NE(output.find("second"), std::string::npos);
	EXPECT_NE(output.find("LogErrPreservesTheStreamedMessageAndManipulators"), std::string::npos) << "function context is present";
}

TEST_F(LogerrCoreFixture, StackDeduplicationIsThreadSafe)
{
	StackTrace::resetDeduplication();
	// Many threads capturing deduplicated traces concurrently must not corrupt the registry or crash. Each worker's stack
	// is distinct (a different thread-entry frame at the base), so every worker legitimately sees a first-seen stack and
	// gets content; the invariant under test is that concurrent registry access is race-free (guarded by its mutex) - no
	// worker throws, and none deadlocks. A data race would surface as a crash, a hang, or a sanitizer report here.
	constexpr int   threadCount = 16;
	std::atomic_int completed{0};
	{
		std::vector<std::jthread> workers;
		workers.reserve(threadCount);
		for (int t = 0; t < threadCount; ++t)
			workers.emplace_back([&] {
				for (int i = 0; i < 8; ++i)
					static_cast<void>(StackTrace(0, /*deduplicateByStack*/ true).data());
				++completed;
			});
	}
	EXPECT_EQ(completed.load(), threadCount) << "every worker finished; concurrent deduplication did not corrupt or hang";

	// After the concurrent storm the registry is still functional: capturing the SAME stack twice deduplicates. Both
	// captures originate from the SAME call site (this one loop line) through the same helper, so the stacks are
	// identical - the first occurrence is contentful, the immediate repeat is suppressed.
	StackTrace::resetDeduplication();
	std::vector<bool> suppressedByIteration;
	for (int i = 0; i < 2; ++i)
		suppressedByIteration.push_back(deduplicatedTraceFromSharedStack().suppressed());
	ASSERT_EQ(suppressedByIteration.size(), 2U);
	EXPECT_FALSE(suppressedByIteration[0]) << "first occurrence of the stack is contentful";
	EXPECT_TRUE(suppressedByIteration[1]) << "an immediate identical-stack repeat is suppressed";
}

TEST_F(LogerrCoreFixture, TracingErrorLineAcceptsAModuleTagAndStillTraces)
{
	logerr::resetTracedSites();
	CoutCapture capture;

	// A module-scoped consumer constructs TracingErrorLine with its own tag; it must show the tag AND still carry the
	// deduped stack trace, identical to the default (APPINFO::name()) form.
	{
		logerr::TracingErrorLine(logerr::sourceFilename(__FILE__), __FILE__, static_cast<std::uint32_t>(__LINE__),
		                         "TestModuleFn", std::string("MySubsystem"))
		    << "module-tagged failure" << std::endl;
	}

	const std::string output = capture.str();
	EXPECT_NE(output.find("[MySubsystem]"), std::string::npos) << "the module tag replaces the app name";
	EXPECT_NE(output.find("module-tagged failure"), std::string::npos);
	EXPECT_NE(output.find("0x"), std::string::npos) << "a module-tagged line still carries the full trace";
}
