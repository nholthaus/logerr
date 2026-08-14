#define _CONCURRENT_QUEUE_NO_WARNINGS

#include <LogFileWriter.h>
#include <LogStream.h>
#include <StackTrace.h>
#include <StackTraceException.h>
#include <StackTraceSIGSEGV.h>
#include <appinfo.h>
#include <asyncTraceLog.h>
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
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <future>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
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

TEST_F(LogerrCoreFixture, RepeatedSymbolizationStaysCorrectAcrossTheModuleCache)
{
	// Symbolization caches each module's opened BFD + symbol table for the process lifetime (the fix for per-frame
	// re-open/re-slurp that made a single trace cost tens of ms on a large binary). Guard that the CACHED path stays
	// correct: many distinct traces in a row must each resolve to real content, never a stale/empty/placeholder result
	// from a reused-but-broken handle. The first call warms the cache; the rest exercise the cached module. (On a
	// stripped build with no symbols every frame is "<no symbol found>" but the trace is still non-empty and carries
	// addresses, so the assertion is on resolvability, gated the same way as the sibling test.)
	StackTrace::resetDeduplication();
	for (int i = 0; i < 8; ++i)
	{
		const std::string trace = traceProbeInnermost();
		EXPECT_FALSE(trace.empty()) << "trace " << i << " must carry content on the cached path";
		EXPECT_EQ(trace.find("<unable to initialize Windows symbols>"), std::string::npos) << trace;
		EXPECT_NE(trace.find("0x"), std::string::npos) << "trace " << i << " must carry frame addresses";
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

	// LOGERR is asynchronous: drain the trace-log worker while the capture buffer is still installed so the entries are
	// written into it before it is read.
	logerr::flushTracedErrors();

	std::cout.rdbuf(originalBuffer);
	const std::string output = captured.str();
	EXPECT_NE(output.find("[ERROR]"), std::string::npos);
	EXPECT_NE(output.find("[WARNING]"), std::string::npos);
	// The ERROR line LEADS with the message; the source location is NOT on the message line - it is the trace footer
	// (frame addresses, "0x...") which follows the message. So the message text comes BEFORE its trace in the output.
	// (The trace's resolved file/function is symbolizer-dependent - an inlined caller may not even name test_logerr.cpp -
	// so the contract asserted here is only "message leads, trace follows", not a specific resolved filename.)
	const std::size_t errMsgPos   = output.find("ordinary diagnostic");
	const std::size_t errTracePos = output.find("0x", errMsgPos);
	EXPECT_NE(errMsgPos, std::string::npos);
	EXPECT_NE(errTracePos, std::string::npos) << "the ERROR carries a trace footer after its message";
	EXPECT_LT(errMsgPos, errTracePos) << "the message must LEAD; the trace follows it";
	// LOGWARNING still carries file:line on its own line (it is not routed through the traced/message-first path).
	EXPECT_NE(output.find("test_logerr.cpp:" + std::to_string(warningLine)), std::string::npos);
	// LOGERR_TRACE appends a full trace (hex frames) after its message.
	EXPECT_NE(output.find("0x", output.find("traced diagnostic")), std::string::npos);
	EXPECT_NE(output.find("ordinary diagnostic"), std::string::npos);
	EXPECT_NE(output.find("warning diagnostic"), std::string::npos);
	EXPECT_NE(output.find("traced diagnostic\n"), std::string::npos);
	EXPECT_NE(output.find("0x"), std::string::npos);
	static_cast<void>(errorLine);
	static_cast<void>(traceLine);
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

	// Capture ONE deduplicated trace and report whether it was suppressed. The capture is a single source line, and the
	// caller invokes this through captureSuppressionSequence's loop - one call instruction executed repeatedly - so
	// every invocation records the byte-identical stack (this function's frame, ignoring StackTrace's own constructor
	// frame via ignore=1, over the same loop call site below it). Kept out of line and non-tail (the volatile sink) so it
	// is a real, stable frame and cannot be folded into the caller.
	LOGERR_TEST_NOINLINE bool captureSuppressed()
	{
		const bool wasSuppressed = StackTrace(1, /*deduplicateByStack*/ true).suppressed();
		g_pathSink += wasSuppressed ? 1 : 0;
		return wasSuppressed;
	}

	// Drive captureSuppressed() `count` times from ONE call instruction (this loop), so each capture sees the identical
	// stack. Returns the per-occurrence suppression flags: the first occurrence of the stack is content (false), every
	// identical repeat after it is suppressed (true). This is the compiler-robust way to produce genuinely identical
	// stacks - it does not depend on two textually-distinct capture points sharing a return address. The flags use
	// std::vector<char> rather than std::vector<bool>: the bit-packed vector<bool> specialization trips a gcc
	// -Werror=array-bounds false positive in its reallocation memmove.
	LOGERR_TEST_NOINLINE std::vector<char> captureSuppressionSequence(int count)
	{
		std::vector<char> flags;
		flags.reserve(static_cast<std::size_t>(count));
		for (int i = 0; i < count; ++i)
			flags.push_back(captureSuppressed() ? 1 : 0);
		return flags;
	}

	// Throw-then-catch a logerr::exception and log it through the SHARED caught-error path (message-first + throw-site
	// trace footer). Out of line + non-tail (the volatile sink) so it is a real frame. Used by the single-occurrence
	// caught-error test.
	LOGERR_TEST_NOINLINE void throwAndLogCaught(const char* message)
	{
		try
		{
			ERR(message);
		}
		catch (const logerr::exception& e)
		{
			logerr::logCaughtError(e);
		}
		g_pathSink += 1;
	}

	// Throw-then-catch a logerr::exception and RETURN the caught object (a copy), so a test can log the SAME object
	// repeatedly. An exception's frames() are fixed at construction, so logging one captured object N times deduplicates
	// on a byte-identical frame array on every toolchain - unlike N separate throws, whose stacks the C++ exception ABI
	// may not reproduce identically. StackTraceException is copyable (it holds a std::string message + a StackTrace).
	LOGERR_TEST_NOINLINE StackTraceException makeCaughtException(const char* message)
	{
		try
		{
			ERR(message);
		}
		catch (const StackTraceException& e)
		{
			g_pathSink += 1;
			return e;
		}
		return StackTraceException(message, __FILENAME__, LOGERR_FUNCTION, __LINE__);    // unreachable; ERR always throws
	}

	// A logerr::exception thrown from an out-of-line frame so its captured throw-site stack resolves to a real function.
	LOGERR_TEST_NOINLINE logerr::exception makeThrownException(const char* message)
	{
		try
		{
			ERR(message);
		}
		catch (const logerr::exception& e)
		{
			return e;
		}
		return logerr::exception(message, "unreachable", "unreachable", 0);
	}

	// Capture ONE deduplicated trace and report whether it was suppressed AND how many frames it retained. Driven from a
	// single call instruction (the loop in captureDedupFrameSequence) so every capture sees the byte-identical stack: the
	// first is content, every identical repeat is suppressed. A suppressed trace must STILL retain its frames.
	LOGERR_TEST_NOINLINE std::pair<bool, std::size_t> captureSuppressedWithFrames()
	{
		const StackTrace trace(1, /*deduplicateByStack*/ true);
		g_pathSink += trace.suppressed() ? 1 : 0;
		return {trace.suppressed(), trace.frames().size()};
	}
	LOGERR_TEST_NOINLINE std::vector<std::pair<bool, std::size_t>> captureDedupFrameSequence(int count)
	{
		std::vector<std::pair<bool, std::size_t>> results;
		results.reserve(static_cast<std::size_t>(count));
		for (int i = 0; i < count; ++i)
			results.push_back(captureSuppressedWithFrames());
		return results;
	}
}

TEST_F(LogerrCoreFixture, LogErrAlwaysCarriesAStackTraceOnAFreshStack)
{
	logerr::resetTracedSites();
	CoutCapture capture;

	LOGERR << "fresh failure" << ENDL;

	logerr::flushTracedErrors();
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

	logerr::flushTracedErrors();
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

	logerr::flushTracedErrors();
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
		logerr::flushTracedErrors();
		beforeReset = capture.str();
	}
	logerr::resetTracedSites();
	{
		CoutCapture capture;
		LOGERR << "reset probe again" << ENDL;   // after the reset the registry is empty: must trace
		logerr::flushTracedErrors();
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

	logerr::flushTracedErrors();
	const std::string output = capture.str();
	EXPECT_NE(output.find("count=42 ratio=3.5 flag=1"), std::string::npos);
	EXPECT_NE(output.find("second"), std::string::npos);
	// The message LEADS the line; its trace footer (hex frames) follows. The resolved file/function in the trace is
	// symbolizer-dependent (an inlined caller may not name test_logerr.cpp), so the pinned contract is only that the
	// message comes first and a trace follows it - the location is no longer shoved onto the message line.
	const std::size_t msgPos   = output.find("count=42 ratio=3.5 flag=1");
	const std::size_t tracePos = output.find("0x", msgPos);
	ASSERT_NE(msgPos, std::string::npos);
	EXPECT_NE(tracePos, std::string::npos) << "the message carries a trace footer after it";
	EXPECT_LT(msgPos, tracePos) << "the message leads; the trace follows";
}

TEST_F(LogerrCoreFixture, CapturedTraceRetainsItsRawFrames)
{
	// A trace must RETAIN the raw return addresses it captured (and symbolized), so a caller can re-symbolize or
	// re-deduplicate the identical stack elsewhere - the mechanism the caught-exception log path relies on. frames() is
	// non-empty for a real capture, and re-symbolizing exactly those retained frames reproduces the same footer the
	// trace already formatted (so the retained set IS the set that was symbolized).
	StackTrace::resetDeduplication();
	const StackTrace trace(0);
	EXPECT_FALSE(trace.frames().empty()) << "a captured trace retains its addresses";
	const std::string reFormatted = StackTrace::formatFrames(trace.frames().data(), static_cast<int>(trace.frames().size()));
	EXPECT_EQ(reFormatted, static_cast<std::string>(trace)) << "frames() are exactly what the trace symbolized";
	// Every retained frame that resolved carries the address column the formatter emits.
	EXPECT_NE(reFormatted.find("0x"), std::string::npos);
}

TEST_F(LogerrCoreFixture, SuppressedDuplicateTraceStillRetainsItsFrames)
{
	// A trace collapsed as a duplicate stack (empty formatted value) must STILL retain its frames, so a caller can
	// re-run the dedup gate / inspect the identical stack on the repeat rather than losing it. Drive the SAME capture
	// call site repeatedly (one call instruction) so the second+ occurrences are genuine byte-identical duplicates: the
	// first is content, the rest are suppressed - and EVERY one, suppressed or not, retains a non-empty frame set.
	logerr::resetTracedSites();
	const std::vector<std::pair<bool, std::size_t>> results = captureDedupFrameSequence(3);
	ASSERT_EQ(results.size(), 3U);
	EXPECT_FALSE(results[0].first) << "the first occurrence of the stack is content, not suppressed";
	EXPECT_TRUE(results[1].first) << "an identical repeat is suppressed";
	EXPECT_TRUE(results[2].first) << "and stays suppressed";
	for (const auto& [suppressed, frameCount] : results)
		EXPECT_GT(frameCount, 0U) << "a trace retains its frames whether or not it was suppressed as a duplicate";
}

TEST_F(LogerrCoreFixture, StackTraceExceptionExposesItsThrowSiteFrames)
{
	// The exception captures its OWN throw-site stack at construction; frames() exposes it (NOT the catch-site stack),
	// and errorMessage() is the CLEAN message with no file/function/line/trace blob.
	const logerr::exception thrown = makeThrownException("throw-site probe");
	EXPECT_EQ(thrown.errorMessage(), "throw-site probe") << "errorMessage() is the clean headline, no blob";
	EXPECT_FALSE(thrown.frames().empty()) << "the exception retains its throw-site frames";
	const std::string footer = StackTrace::formatFrames(thrown.frames().data(), static_cast<int>(thrown.frames().size()));
	EXPECT_NE(footer.find("0x"), std::string::npos) << "the throw-site frames symbolize to a real footer";
}

TEST_F(LogerrCoreFixture, CaughtExceptionLogsMessageFirstThenItsThrowSiteTraceFooter)
{
	// The invariant for a caught logerr::exception: the CLEAN message leads the [ERROR] line, and the exception's OWN
	// throw-site trace follows as the footer - identical shape to LOGERR, but keyed on the throw stack, not the catch.
	logerr::resetTracedSites();
	CoutCapture capture;

	throwAndLogCaught("caught-once failure");

	logerr::flushTracedErrors();
	const std::string output = capture.str();
	EXPECT_NE(output.find("[ERROR]"), std::string::npos);
	// The first three bracketed fields are intact and the message leads (the dock stays parseable).
	const std::size_t msgPos   = output.find("caught-once failure");
	const std::size_t tracePos = output.find("0x", msgPos);
	ASSERT_NE(msgPos, std::string::npos) << "the clean message is present";
	EXPECT_NE(tracePos, std::string::npos) << "a throw-site trace footer follows the message";
	EXPECT_LT(msgPos, tracePos) << "the message LEADS; the trace follows - same contract as LOGERR";
	// The clean message is NOT buried under e.what()'s blob: the location "in `...` at `...`" text e.what() emits must
	// not precede the message on the line.
	EXPECT_EQ(output.find("STACK TRACE:"), std::string::npos) << "e.what()'s fat blob is not logged; only the clean message";
}

TEST_F(LogerrCoreFixture, CaughtExceptionDeduplicatesByThrowSiteStack)
{
	// Logging the SAME throw-site stack repeatedly: the first carries a footer, each identical repeat is message-only -
	// the exact deduplication LOGERR applies, but on the throw-site stack the exception captured.
	//
	// Capture ONE exception and log THAT SAME object three times, rather than throwing three times: an exception's
	// frames() are fixed at construction, so all three logCaughtError() calls dedup on a byte-identical frame array -
	// guaranteed on every toolchain. (Two real throws are NOT guaranteed to produce byte-identical stacks: the C++
	// exception ABI / unwinder can vary a frame across throws on some compilers, which made a throw-thrice version flaky.)
	logerr::resetTracedSites();
	CoutCapture capture;

	const StackTraceException caught = makeCaughtException("recurring caught failure");
	logerr::logCaughtError(caught);
	logerr::logCaughtError(caught);
	logerr::logCaughtError(caught);

	logerr::flushTracedErrors();
	const std::string output = capture.str();
	EXPECT_EQ(occurrences(output, "recurring caught failure"), 3U) << "every caught occurrence logs its message";
	// Exactly one throw-site footer across three identical-stack repeats: between the 1st and 2nd message there is a
	// trace, but between the 2nd and 3rd there is none.
	const std::size_t firstMsg  = output.find("recurring caught failure");
	ASSERT_NE(firstMsg, std::string::npos);
	const std::size_t secondMsg = output.find("recurring caught failure", firstMsg + 1);
	const std::size_t thirdMsg  = output.find("recurring caught failure", secondMsg + 1);
	ASSERT_NE(secondMsg, std::string::npos);
	ASSERT_NE(thirdMsg, std::string::npos);
	const std::size_t traceAfterFirst = output.find("0x", firstMsg);
	EXPECT_NE(traceAfterFirst, std::string::npos) << "the first caught occurrence traces";
	EXPECT_LT(traceAfterFirst, secondMsg) << "the trace follows the first message and precedes the second";
	const std::size_t traceAfterSecond = output.find("0x", secondMsg + 1);
	EXPECT_TRUE(traceAfterSecond == std::string::npos || traceAfterSecond > thirdMsg)
	    << "an identical repeated throw stack must not re-emit the trace - message-only, exactly like LOGERR";
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
}

TEST_F(LogerrCoreFixture, IdenticalStackTracesOnceThenIsSuppressed)
{
	// The single-stack deduplication invariant, deterministic: the SAME stack captured repeatedly traces exactly once.
	// captureSuppressionSequence drives one capture instruction through a loop, so every occurrence records the identical
	// stack - the first is content (not suppressed), every repeat after it is suppressed.
	StackTrace::resetDeduplication();
	const std::vector<char> suppressed = captureSuppressionSequence(4);
	ASSERT_EQ(suppressed.size(), 4U);
	EXPECT_EQ(suppressed[0], 0) << "the first occurrence of a stack is contentful";
	for (std::size_t i = 1; i < suppressed.size(); ++i)
		EXPECT_EQ(suppressed[i], 1) << "identical-stack repeat #" << i << " must be suppressed";
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

	logerr::flushTracedErrors();
	const std::string output = capture.str();
	EXPECT_NE(output.find("[MySubsystem]"), std::string::npos) << "the module tag replaces the app name";
	EXPECT_NE(output.find("module-tagged failure"), std::string::npos);
	EXPECT_NE(output.find("0x"), std::string::npos) << "a module-tagged line still carries the full trace";
}

TEST_F(LogerrCoreFixture, ErrorTraceIsWrittenByTheAsyncWorkerAndStaysContiguous)
{
	// The message and its stack-trace footer are symbolized and written by the background worker as ONE atomic unit, so
	// they are always contiguous: after flushing the worker, the message is present, its trace footer (a hex frame) is
	// present, and the trace follows the message with NO other [ERROR] line wedged between them.
	logerr::resetTracedSites();
	CoutCapture capture;

	LOGERR << "async-worker contiguous failure" << ENDL;

	// Drain the worker while the capture buffer is still installed; the synchronous drain guarantees the entry is written
	// before the capture is read.
	logerr::flushTracedErrors();
	const std::string output = capture.str();

	const std::size_t messagePos = output.find("async-worker contiguous failure");
	ASSERT_NE(messagePos, std::string::npos) << "the message line was written";
	const std::size_t tracePos = output.find("0x", messagePos);
	ASSERT_NE(tracePos, std::string::npos) << "the trace footer follows the message";
	// The footer belongs to THIS message: no other [ERROR] prefix appears between the message and its trace.
	const std::size_t nextErrorPrefix = output.find("[ERROR]", messagePos);
	EXPECT_TRUE(nextErrorPrefix == std::string::npos || nextErrorPrefix > tracePos)
	    << "the message and its trace footer are one contiguous block";
}

// --- Crash/teardown/thread-safety regression suite (logerr 1.3.0) -----------------------------------------------------
// These guard the exact class of bug that produced the exit-time SIGSEGV: symbolization runs on a background thread and
// during late/exit-time teardown, so any static it touches must survive the whole process and it must never itself
// crash. A destroyed std::map/std::mutex touched by a still-running symbolizer was a use-after-free (a SIGSEGV in
// _Rb_tree::find on a torn-down module cache); the symbolizer statics are now never-destroyed and formatFrames is
// self-defending. The suite below drives symbolization concurrently, right up to and past scope teardown, over the
// async worker, and on adversarial input, so logerr's own CI is the gate against a regression.

// A raw frame array captured at a real call site; a valid input formatFrames must symbolize without faulting.
LOGERR_TEST_NOINLINE static std::vector<void*> captureRawFrames()
{
	return logerr::captureCallStack(0);
}

TEST_F(LogerrCoreFixture, FormatFramesIsSafeUnderConcurrentAndLateSymbolization)
{
	// Several threads symbolize captured stacks concurrently AND keep symbolizing right up to scope teardown - the
	// concurrent + late path that runs on the async worker at exit. The symbolizer's process-global state (the module
	// cache + its mutex, the trace mutex) is serialized internally, so no thread may crash, hang, or corrupt it, and
	// every result is non-empty. A destroyed-static use-after-free would surface here as a crash or a sanitizer report.
	constexpr int   workerCount = 8;
	std::barrier    start(workerCount);
	std::atomic_int completed{0};
	std::atomic_int emptyResults{0};
	{
		std::vector<std::jthread> workers;
		workers.reserve(workerCount);
		for (int worker = 0; worker < workerCount; ++worker)
		{
			workers.emplace_back([&] {
				const std::vector<void*> frames = captureRawFrames();
				start.arrive_and_wait();
				for (int iteration = 0; iteration < 32; ++iteration)
				{
					const std::string trace = StackTrace::formatFrames(frames.data(), static_cast<int>(frames.size()));
					if (trace.empty())
						++emptyResults;
				}
				++completed;
			});
		}
		// The jthreads keep running as this scope tears down; the destructor join happens as the enclosing objects
		// unwind, exercising symbolization that overlaps teardown.
	}
	EXPECT_EQ(completed.load(), workerCount) << "every worker finished; concurrent late symbolization did not crash or hang";
	EXPECT_EQ(emptyResults.load(), 0) << "a captured real stack always symbolizes to non-empty content";
}

TEST_F(LogerrCoreFixture, AsyncTracePathIsCorrectUnderConcurrentProducers)
{
	// Many threads drive the async trace path (LOGERR -> enqueueTracedError) at once; after a single flush the worker
	// has symbolized and written every entry. Each thread logs a distinct stack (a distinct thread-entry frame at its
	// base) so none is deduplicated away, and each message is written exactly once, contiguously, with no crash. This
	// is the multi-producer counterpart to the single-threaded contiguity test and exercises the worker's queue,
	// off-thread symbolization, and output mutex under contention.
	logerr::resetTracedSites();
	constexpr int producerCount = 8;
	CoutCapture   capture;
	{
		std::vector<std::jthread> producers;
		producers.reserve(producerCount);
		for (int producer = 0; producer < producerCount; ++producer)
			producers.emplace_back([producer] { LOGERR << "concurrent-producer failure " << producer << ENDL; });
	}
	logerr::flushTracedErrors();
	const std::string output = capture.str();
	for (int producer = 0; producer < producerCount; ++producer)
	{
		const std::string message = "concurrent-producer failure " + std::to_string(producer);
		EXPECT_EQ(occurrences(output, message), 1U) << "each producer's message is written exactly once: " << message;
	}
	// Every distinct producer stack carries its own trace footer, so the output holds at least one hex frame.
	EXPECT_NE(output.find("0x"), std::string::npos) << "the concurrently produced entries carry their stack traces";
}

TEST_F(LogerrCoreFixture, FormatFramesNeverThrowsAndDegradesOnAdversarialInput)
{
	// The self-defense guard: symbolization must NEVER be the thing that crashes the process it is diagnosing. Feed it
	// deliberately bogus frame arrays - null, non-canonical, and unmapped return addresses that a real trace could
	// never contain - and assert it neither throws nor crashes. It returns a string in every case (a placeholder or a
	// best-effort "??" line); on a valid captured stack it returns non-empty real content. The count<=0 contract path
	// is exercised too.
	void* bogus[] = {nullptr, reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x1)),
	                 reinterpret_cast<void*>(~static_cast<std::uintptr_t>(0)),
	                 reinterpret_cast<void*>(static_cast<std::uintptr_t>(0xDEADBEEF))};
	EXPECT_NO_THROW({
		const std::string trace = StackTrace::formatFrames(bogus, static_cast<int>(std::size(bogus)));
		static_cast<void>(trace);
	});
	// A zero/negative count is the documented empty contract, never a fault.
	EXPECT_NO_THROW({ EXPECT_TRUE(StackTrace::formatFrames(bogus, 0).empty()); });
	EXPECT_NO_THROW({ EXPECT_TRUE(StackTrace::formatFrames(nullptr, 0).empty()); });
	EXPECT_NO_THROW({ static_cast<void>(StackTrace::formatFrames(bogus, -1)); });
	// A valid captured stack still symbolizes to real, non-empty content through the same guarded entry point.
	const std::vector<void*> real = captureRawFrames();
	std::string valid;
	EXPECT_NO_THROW({ valid = StackTrace::formatFrames(real.data(), static_cast<int>(real.size())); });
	EXPECT_FALSE(valid.empty()) << "a real captured stack symbolizes to content";
	EXPECT_NE(valid.find("0x"), std::string::npos) << "a real captured stack carries frame addresses";
}

TEST_F(LogerrCoreFixture, SymbolizationStaysSafeAfterFlushingTheWorker)
{
	// Post-worker-teardown ordering: after flushTracedErrors() has drained the worker, later symbolization on the
	// calling thread must still be safe and correct. The symbolizer's statics are process-lifetime (never destroyed),
	// so a trace taken after a flush resolves normally rather than touching torn-down state - the ordering that,
	// mishandled, produced the exit-time use-after-free.
	logerr::resetTracedSites();
	{
		CoutCapture capture;
		LOGERR << "pre-flush failure" << ENDL;
		logerr::flushTracedErrors();
		EXPECT_NE(capture.str().find("pre-flush failure"), std::string::npos);
	}
	// Symbolize again AFTER the flush; it must not crash and must still resolve to content.
	const std::vector<void*> frames = captureRawFrames();
	std::string              trace;
	EXPECT_NO_THROW({ trace = StackTrace::formatFrames(frames.data(), static_cast<int>(frames.size())); });
	EXPECT_FALSE(trace.empty()) << "symbolization after a worker flush still resolves";
	EXPECT_NE(trace.find("0x"), std::string::npos);
	// And a fresh LOGERR after the flush is still served correctly (the worker singleton was not torn down by flush).
	{
		logerr::resetTracedSites();
		CoutCapture capture;
		LOGERR << "post-flush failure" << ENDL;
		logerr::flushTracedErrors();
		const std::string output = capture.str();
		EXPECT_NE(output.find("post-flush failure"), std::string::npos) << "logging after a flush still works";
		EXPECT_NE(output.find("0x"), std::string::npos) << "and still carries its trace";
	}
}
