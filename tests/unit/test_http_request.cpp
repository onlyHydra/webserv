#include <catch2/catch_all.hpp>
#include "HttpRequest.h"

void	test(const std::string &in)
{
	HttpRequest test;
	bool	result;

	result = test.parse(in.c_str(), in.length());
	REQUIRE(result == false);
	REQUIRE(test.getState() == ERROR);
	REQUIRE(errno == ERR_HTTP_BAD_REQUEST);
}

TEST_CASE("HTTP Request", "[https]")
{
	SECTION("Uninitialised values")
	{
		HttpRequest x;
		size_t number = 0;

		REQUIRE(x.getBodyLength() == 0);
		REQUIRE(x.getBuffer() == "");
		REQUIRE(x.getHttpVer() == "");
		REQUIRE(x.getMethod() == "");
		REQUIRE(x.getPath() == "");
		REQUIRE(x.getQuery() == "");
		REQUIRE(x.getSessId() == "");
		REQUIRE(x.getUri() == "");
		REQUIRE(x.getHeaders().getLength() == 0);
		REQUIRE(x.getCookies().getLength() == 0);
		REQUIRE(x.getBody().size() == 0);
		x.extension();
		x.keepAlive();
		x.headerAsSize("", number);
		x.parse("", 0);
	}
	SECTION("Invalid Start Line")
	{
		test("\r\n");
		test("A\r\n");
		test("AB\r\n");
		test("GET\r\n");
		test("GET \r\n");
		test("GET /path\r\n");
		test("GET /path \r\n");
	}
	SECTION("Invalid Header")
	{
		test("GET /path Https\r\nblablabla\r\n");
		test("GET /path Https\r\nblablabla=bliblablub\r\n");
		test("GET /path Https\r\nblabla\rbl\na\r\n");
	}
	SECTION("Invalid Body")
	{
		{
			// Invalid, because no Content Length, but body given
			HttpRequest x;
			x.parse("PUT /path Https\r\nkey: value\r\n\r\n", 31);
			REQUIRE(x.parse("bliblablub\r\n", 12) == false);
			REQUIRE(errno == ERR_HTTP_BAD_REQUEST);
		}
		{
			// Invalid, because GET may not have a body
			HttpRequest x;
			x.parse("GET /path Https\r\nContent-Length: 5\r\nkey: value\r\n\r\n", 50);
			REQUIRE(x.parse("12345", 5) == false);
			REQUIRE(errno == ERR_HTTP_BAD_REQUEST);
		}
		{
			// Invalid, because GET may not have a body
			HttpRequest x;
			x.parse("PUT /path Https\r\nContent-Length:blablabla\r\nkey: value\r\n\r\n", 57);
			REQUIRE(x.parse("12345", 5) == false);
			REQUIRE(errno == ERR_HTTP_BAD_REQUEST);
		}
	}
	SECTION("Content Length Tests")
	{
		{
			// Invalid, because Content Length too low
			HttpRequest x;
			x.parse("PUT /path Https\r\nContent-Length:0\r\nkey: value\r\n\r\n", 50);
			REQUIRE(x.parse("12345", 5) == false);
			REQUIRE(errno == ERR_HTTP_BAD_REQUEST);
		}
		{
			// Invalid, because Content Length too low
			HttpRequest x;
			x.parse("PUT /path Https\r\nContent-Length: 2\r\nkey: value\r\n\r\n", 50);
			REQUIRE(x.parse("12345", 5) == false);
			REQUIRE(errno == ERR_HTTP_BAD_REQUEST);
		}
		{
			// Invalid, because Content Length too low
			HttpRequest x;
			x.parse("PUT /path Https\r\nContent-Length: 5\r\nkey: value\r\n\r\n", 50);
			REQUIRE(x.parse("abcd", 4) == true);
			REQUIRE(x.parse("12345", 5) == false);
			REQUIRE(errno == ERR_HTTP_BAD_REQUEST);
		}
		{ 
			// Invalid Content Length
			HttpRequest x;
			errno = 0;
			REQUIRE(x.parse("PUT / Http\r\nContent-Length:bla\r\nkey:value\r\n\r\nI", 46) == false);
			REQUIRE(errno == ERR_HTTP_BAD_REQUEST);
		}
		{
			// Negative value for Content Length
			HttpRequest x;
			errno = 0;
			REQUIRE(x.parse("PUT / Http\r\nContent-Length: -1\r\nkey:value\r\n\r\nI", 46) == false);
			REQUIRE(errno == ERR_HTTP_BAD_REQUEST);
		}
		{
			// Too big of a value for Content Length
			HttpRequest x;
			errno = 0;
			REQUIRE(x.parse("PUT / Http\r\nContent-Length:99999999999999999999999999\r\nkey:value\r\n\r\nI", 69) == false);
			REQUIRE(errno == ERR_HTTP_BAD_REQUEST);
		}
		{
			// MAX_INT Content Length
			HttpRequest x;
			errno = 0;
			REQUIRE(x.parse("PUT / Http\r\nContent-Length:2147483647\r\nkey:value\r\n\r\nI", 53) == true);
			REQUIRE(errno == 0);
		}
		{
			// MAX_INT +1  Content Length
			HttpRequest x;
			errno = 0;
			REQUIRE(x.parse("PUT / Http\r\nContent-Length:2147483648\r\nkey:value\r\n\r\nI", 53) == false);
			REQUIRE(errno == ERR_HTTP_BAD_REQUEST);
		}
	}
	SECTION("Valid Start Line")
	{
		// Testing methods
		{
			HttpRequest x;
			errno = 0;
			REQUIRE(x.parse("PUT /path Https\r\n", 17) == true);
			REQUIRE(x.getMethod() == "PUT");
			REQUIRE(x.getPath() == "/path");
			REQUIRE(x.getQuery() == "");
			REQUIRE(x.getHttpVer() == "Https");
			REQUIRE(x.getState() == HEADER);
			REQUIRE(errno == 0);
		}
		{
			HttpRequest x;
			errno = 0;
			REQUIRE(x.parse("PATCH / HttpVersion\r\n", 21) == true);
			REQUIRE(x.getMethod() == "PATCH");
			REQUIRE(x.getPath() == "/");
			REQUIRE(x.getQuery() == "");
			REQUIRE(x.getHttpVer() == "HttpVersion");
			REQUIRE(x.getState() == HEADER);
			REQUIRE(errno == 0);
		}
		{
			HttpRequest x;
			errno = 0;
			REQUIRE(x.parse("GET /root/dir Google.com\r\n", 26) == true);
			REQUIRE(x.getMethod() == "GET");
			REQUIRE(x.getPath() == "/root/dir");
			REQUIRE(x.getQuery() == "");
			REQUIRE(x.getHttpVer() == "Google.com");
			REQUIRE(x.getState() == HEADER);
			REQUIRE(errno == 0);
		}
		{
			HttpRequest x;
			errno = 0;
			REQUIRE(x.parse("DELETE /root/home Google.com\r\n", 30) == true);
			REQUIRE(x.getMethod() == "DELETE");
			REQUIRE(x.getPath() == "/root/home");
			REQUIRE(x.getQuery() == "");
			REQUIRE(x.getHttpVer() == "Google.com");
			REQUIRE(x.getState() == HEADER);
			REQUIRE(errno == 0);
		}
		{
			HttpRequest x;
			errno = 0;
			REQUIRE(x.parse("POST //|\\ Amazon.xxx\r\n", 22) == true);
			REQUIRE(x.getMethod() == "POST");
			REQUIRE(x.getPath() == "//|\\");
			REQUIRE(x.getQuery() == "");
			REQUIRE(x.getHttpVer() == "Amazon.xxx");
			REQUIRE(x.getState() == HEADER);
			REQUIRE(errno == 0);
		}
		{
			HttpRequest x;
			errno = 0;
			REQUIRE(x.parse("POST /index.html HttpVersion/1.1\r\n", 34) == true);
			REQUIRE(x.getMethod() == "POST");
			REQUIRE(x.getPath() == "/index.html");
			REQUIRE(x.getQuery() == "");
			REQUIRE(x.getHttpVer() == "HttpVersion/1.1");
			REQUIRE(x.getState() == HEADER);
			REQUIRE(errno == 0);
		}
		{
			HttpRequest x;
			errno = 0;
			REQUIRE(x.parse("POST /index.html?key=value HttpVersion/1.1\r\n", 44) == true);
			REQUIRE(x.getMethod() == "POST");
			REQUIRE(x.getPath() == "/index.html");
			REQUIRE(x.getQuery() == "key=value");
			REQUIRE(x.getHttpVer() == "HttpVersion/1.1");
			REQUIRE(x.getState() == HEADER);
			REQUIRE(errno == 0);
		}
	}
	SECTION("Valid Headers Line")
	{
		// Testing Headers
		{
			HttpRequest x;
			errno = 0;
			REQUIRE(x.parse("GET / Http\r\nkey:value\r\n", 23) == true);
			REQUIRE(errno == 0);
		}
		{
			HttpRequest x;
			errno = 0;
			REQUIRE(x.parse("GET / Http\r\nkey:    value\r\n", 27) == true);
			REQUIRE(errno == 0);
		}
		{
			HttpRequest x;
			errno = 0;
			REQUIRE(x.parse("GET / Http\r\nkey:value\r\nabc: 123\r\n\r\n", 35) == true);
			REQUIRE(errno == 0);
		}
	}
	SECTION("Valid Body Line")
	{
		// Testing Headers
		{
			HttpRequest x;
			errno = 0;
			REQUIRE(x.parse("PUT / Http\r\nContent-Length: 1\r\nkey:value\r\n\r\na", 45) == true);
			REQUIRE(errno == 0);
		}
		{
			HttpRequest x;
			errno = 0;
			REQUIRE(x.parse("PUT / Http\r\nContent-Length:10\r\nkey:value\r\n\r\nI am fish\n", 54) == true);
			REQUIRE(errno == 0);
		}
	}
}