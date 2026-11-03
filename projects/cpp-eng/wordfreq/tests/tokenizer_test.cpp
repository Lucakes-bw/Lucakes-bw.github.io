#include "wordfreq/tokenizer.h"

#include <gtest/gtest.h>

using wordfreq::tokenize;
using Words = std::vector<std::string>;

TEST(Tokenizer, SplitsOnSpacesAndPunctuation)
{
    EXPECT_EQ(tokenize("Hello, world!"), (Words{"hello", "world"}));
    EXPECT_EQ(tokenize("  one\ttwo\nthree  "), (Words{"one", "two", "three"}));
}

TEST(Tokenizer, EmptyAndSeparatorOnlyInput)
{
    EXPECT_TRUE(tokenize("").empty());
    EXPECT_TRUE(tokenize(" ,.;!? \n").empty());
}

TEST(Tokenizer, ConvertsToLowerCase)
{
    EXPECT_EQ(tokenize("The THE tHe"), (Words{"the", "the", "the"}));
}

TEST(Tokenizer, KeepsApostrophesInsideWords)
{
    EXPECT_EQ(tokenize("don't it's"), (Words{"don't", "it's"}));
}

TEST(Tokenizer, StripsApostrophesAroundWords)
{
    EXPECT_EQ(tokenize("'quoted' ''' rock'n'roll'"), (Words{"quoted", "rock'n'roll"}));
}

TEST(Tokenizer, DigitsArePartOfWords)
{
    EXPECT_EQ(tokenize("R2D2 and 3.14"), (Words{"r2d2", "and", "3", "14"}));
}

// 非 ASCII 的字节都是分隔符：这是已知的限制，测试把它固定下来，以后改了规则测试会提醒
TEST(Tokenizer, NonAsciiBytesAreSeparators)
{
    EXPECT_EQ(tokenize("caf\xc3\xa9 na\xc3\xafve"), (Words{"caf", "na", "ve"}));
}
