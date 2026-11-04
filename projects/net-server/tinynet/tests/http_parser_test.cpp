#include "tinynet/http/HttpParser.h"

#include <gtest/gtest.h>

#include "tinynet/Buffer.h"

using tinynet::Buffer;
using tinynet::http::HttpParser;
using Result = HttpParser::Result;

TEST(HttpParser, CompleteRequest)
{
    Buffer buf;
    buf.append("GET /index.html?a=1&b=2 HTTP/1.1\r\nHost: example.com\r\nUser-Agent:  curl/8 \r\n\r\n");
    HttpParser p;
    ASSERT_EQ(p.parse(&buf), Result::kGotRequest);
    const auto& r = p.request();
    EXPECT_EQ(r.method, "GET");
    EXPECT_EQ(r.path, "/index.html");
    EXPECT_EQ(r.query, "a=1&b=2");
    EXPECT_EQ(r.version, "HTTP/1.1");
    EXPECT_EQ(r.header("host"), "example.com");
    EXPECT_EQ(r.header("user-agent"), "curl/8");     // 名字转成小写，值去掉两端的空格
    EXPECT_EQ(buf.readableBytes(), 0u);
}

// 数据一个字节一个字节地到达：每次都能接着上次的位置解析
TEST(HttpParser, ByteByByte)
{
    std::string req = "POST /submit HTTP/1.1\r\nContent-Length: 5\r\n\r\nhello";
    Buffer buf;
    HttpParser p;
    Result r = Result::kNeedMore;
    for (char ch : req) {
        buf.append(std::string(1, ch));
        r = p.parse(&buf);
        if (r != Result::kNeedMore) break;
    }
    ASSERT_EQ(r, Result::kGotRequest);
    EXPECT_EQ(p.request().method, "POST");
    EXPECT_EQ(p.request().body, "hello");
}

// 一次收到两个请求（流水线）：解析完第一个，剩下的留在缓冲区中
TEST(HttpParser, Pipelined)
{
    Buffer buf;
    buf.append("GET /a HTTP/1.1\r\n\r\nGET /b HTTP/1.1\r\n\r\n");
    HttpParser p;
    ASSERT_EQ(p.parse(&buf), Result::kGotRequest);
    EXPECT_EQ(p.request().path, "/a");
    p.reset();
    ASSERT_EQ(p.parse(&buf), Result::kGotRequest);
    EXPECT_EQ(p.request().path, "/b");
}

TEST(HttpParser, BadRequests)
{
    const char* bad[] = {
        "GET\r\n\r\n",                      // 缺少路径和版本
        "GET /  HTTP/1.1\r\n\r\n",          // 两个空格
        "GET / HTTP/2.0\r\n\r\n",           // 不支持的版本
        "GET / HTTP/1.1\r\nNoColon\r\n\r\n",
        "POST / HTTP/1.1\r\nContent-Length: abc\r\n\r\n",
    };
    for (const char* s : bad) {
        Buffer buf;
        buf.append(s);
        HttpParser p;
        EXPECT_EQ(p.parse(&buf), Result::kBadRequest) << s;
    }
}

// 头部太长：不能无限制地等下去，否则一个连接就能占用任意多的内存
TEST(HttpParser, HeaderTooLong)
{
    Buffer buf;
    buf.append("GET / HTTP/1.1\r\nX-Long: " + std::string(HttpParser::kMaxHeaderBytes, 'a'));
    HttpParser p;
    EXPECT_EQ(p.parse(&buf), Result::kBadRequest);
}

TEST(HttpRequest, KeepAlive)
{
    auto parse = [](const std::string& s) {
        Buffer buf;
        buf.append(s);
        HttpParser p;
        p.parse(&buf);
        return p.request().keepAlive();
    };
    EXPECT_TRUE(parse("GET / HTTP/1.1\r\n\r\n"));
    EXPECT_FALSE(parse("GET / HTTP/1.1\r\nConnection: close\r\n\r\n"));
    EXPECT_FALSE(parse("GET / HTTP/1.0\r\n\r\n"));
    EXPECT_TRUE(parse("GET / HTTP/1.0\r\nConnection: Keep-Alive\r\n\r\n"));
}
