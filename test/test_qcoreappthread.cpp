#include <qCoreAppThread.h>

#include <gtest/gtest.h>

#include <QCoreApplication>

class QCoreAppThreadFixture : public ::testing::Test {};

TEST_F(QCoreAppThreadFixture, CreatesSharesAndReleasesAnApplicationWhenNoneExists)
{
	ASSERT_EQ(QCoreApplication::instance(), nullptr);
	auto first = QCoreAppThread::instance();
	ASSERT_NE(first, nullptr);
	EXPECT_NE(QCoreApplication::instance(), nullptr);
	auto second = QCoreAppThread::instance();
	EXPECT_EQ(first, second);
	first.reset();
	EXPECT_NE(QCoreApplication::instance(), nullptr);
	second.reset();
	EXPECT_EQ(QCoreApplication::instance(), nullptr);
}
