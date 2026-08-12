#include <coreApplication.h>
#include <logerr>

#include <gtest/gtest.h>

#include <QEvent>
#include <QObject>

#include <stdexcept>

namespace
{
	class CoreApplicationFixture : public ::testing::Test
	{
	protected:
		void SetUp() override { static_cast<void>(logerr::takeException()); }
		void TearDown() override { static_cast<void>(logerr::takeException()); }
	};

	class EventTarget : public QObject
	{
	public:
		enum class Behavior { Accept, NonFatalLogerr, FatalLogerr, StandardException, UnknownException };

		explicit EventTarget(Behavior behavior) : behavior(behavior) {}

		bool event(QEvent*) override
		{
			switch (behavior)
			{
				case Behavior::Accept: return true;
				case Behavior::NonFatalLogerr: ERR("nonfatal event failure");
				case Behavior::FatalLogerr: FATAL_ERR("fatal event failure");
				case Behavior::StandardException: throw std::runtime_error("standard event failure");
				case Behavior::UnknownException: throw 42;
			}
			return false;
		}

		Behavior behavior;
	};
}

TEST_F(CoreApplicationFixture, NormalEventsReturnTheWrappedQtResult)
{
	EventTarget target(EventTarget::Behavior::Accept);
	QEvent event(QEvent::User);
	EXPECT_TRUE(QCoreApplication::instance()->notify(&target, &event));
}

TEST_F(CoreApplicationFixture, NonFatalLogerrExceptionsAreReportedAndSwallowed)
{
	EventTarget target(EventTarget::Behavior::NonFatalLogerr);
	QEvent event(QEvent::User);
	EXPECT_FALSE(QCoreApplication::instance()->notify(&target, &event));
}

TEST_F(CoreApplicationFixture, FatalAndUnknownEventExceptionsPropagate)
{
	QEvent event(QEvent::User);
	EventTarget fatal(EventTarget::Behavior::FatalLogerr);
	EventTarget standard(EventTarget::Behavior::StandardException);
	EventTarget unknown(EventTarget::Behavior::UnknownException);
	EXPECT_THROW(QCoreApplication::instance()->notify(&fatal, &event), logerr::exception);
	EXPECT_THROW(QCoreApplication::instance()->notify(&standard, &event), std::runtime_error);
	EXPECT_ANY_THROW(QCoreApplication::instance()->notify(&unknown, &event));
}

TEST_F(CoreApplicationFixture, PendingWorkerExceptionsAreRethrownThroughNotify)
{
	EventTarget target(EventTarget::Behavior::Accept);
	QEvent event(QEvent::User);
	logerr::captureException(std::make_exception_ptr(std::runtime_error("background failure")));
	EXPECT_THROW(QCoreApplication::instance()->notify(&target, &event), std::runtime_error);
	EXPECT_EQ(logerr::takeException(), nullptr);
}

int main(int argc, char** argv)
{
	CoreApplication app(argc, argv);
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
