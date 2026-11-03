#include "wordfreq/counter.h"

#include <gtest/gtest.h>

using wordfreq::Counter;
using wordfreq::Entry;

namespace {

// 测试夹具：每个 TEST_F 开始前都会构造一个新的 CounterTest，counter 中已经统计好了 text
class CounterTest : public ::testing::Test {
protected:
    void SetUp() override { counter.add("the cat and the hat. The end, and the END"); }
    Counter counter;
};

}  // namespace

TEST_F(CounterTest, CountsTotalAndUnique)
{
    EXPECT_EQ(counter.total(), 10);
    EXPECT_EQ(counter.unique(), 5u);
}

TEST_F(CounterTest, CountOfEachWord)
{
    EXPECT_EQ(counter.count("the"), 4);
    EXPECT_EQ(counter.count("end"), 2);
    EXPECT_EQ(counter.count("cat"), 1);
    EXPECT_EQ(counter.count("dog"), 0);
}

TEST_F(CounterTest, TopSortsByCountThenWord)
{
    // and 和 end 都是 2 次，按字典序 and 在前
    std::vector<Entry> expected{{"the", 4}, {"and", 2}, {"end", 2}};
    EXPECT_EQ(counter.top(3), expected);
}

TEST_F(CounterTest, TopWithKLargerThanUnique)
{
    EXPECT_EQ(counter.top(100).size(), 5u);
    EXPECT_TRUE(counter.top(0).empty());
}

TEST_F(CounterTest, Merge)
{
    Counter other;
    other.add("cat dog");
    counter.merge(other);
    EXPECT_EQ(counter.total(), 12);
    EXPECT_EQ(counter.count("cat"), 2);
    EXPECT_EQ(counter.count("dog"), 1);
}

TEST(Counter, EmptyCounter)
{
    Counter c;
    EXPECT_EQ(c.total(), 0);
    EXPECT_EQ(c.unique(), 0u);
    EXPECT_TRUE(c.top(10).empty());
}

// 参数化测试：线程数从 1 到 8，多线程统计的结果都要和单线程完全相同
class ParallelTest : public ::testing::TestWithParam<unsigned> {};

TEST_P(ParallelTest, SameAsSingleThread)
{
    std::string text;
    for (int i = 0; i < 200; ++i) text += "alpha beta, gamma! alpha's beta-gamma\n";
    Counter single;
    single.add(text);
    Counter parallel = wordfreq::countParallel(text, GetParam());
    EXPECT_EQ(parallel.total(), single.total());
    EXPECT_EQ(parallel.top(10), single.top(10));
}

INSTANTIATE_TEST_SUITE_P(Threads, ParallelTest, ::testing::Values(1u, 2u, 3u, 4u, 8u));

// 覆盖率报告发现的两个没有测到的分支
TEST(CountParallel, MoreThreadsThanText)
{
    Counter c = wordfreq::countParallel("a b", 8);  // 8 个线程，文本只有 3 个字节
    EXPECT_EQ(c.total(), 2);
    EXPECT_EQ(wordfreq::countParallel("", 4).total(), 0);
}

TEST(CountParallel, ZeroThreadsMeansOne)
{
    EXPECT_EQ(wordfreq::countParallel("x y z", 0).total(), 3);
}
