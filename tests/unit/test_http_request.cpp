#include <catch2/catch_all.hpp>
#include "HttpRequest.h"
#include "HTTPCODES.h"

void	test(const std::string &in, int code)
{
	HttpRequest test;
	bool	result;

	result = test.parse(in.c_str(), in.length());
	REQUIRE(result == false);
	REQUIRE(test.getState() == ERROR);
	REQUIRE(errno == code);
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
		test("\r\n", HTTP_BAD_REQUEST);
		test("A\r\n", HTTP_BAD_REQUEST);
		test("AB\r\n", HTTP_BAD_REQUEST);
		test("GET\r\n", HTTP_BAD_REQUEST);
		test("GET \r\n", HTTP_BAD_REQUEST);
		test("GET /path\r\n", HTTP_BAD_REQUEST);
		test("GET /path \r\n", HTTP_VERSION_NOT_SUPP);
	}
	SECTION("Invalid Header")
	{
		test("GET /path HTTP/1.1\r\nblablabla\r\n", HTTP_BAD_REQUEST);
		test("GET /path HTTP/1.1\r\nblablabla=bliblablub\r\n", HTTP_BAD_REQUEST);
		test("GET /path HTTP/1.1\r\nblabla\rbl\na\r\n", HTTP_BAD_REQUEST);
	}
	SECTION("Invalid Body")
	{
		{
			// Invalid, because no Content Length, but body given
			HttpRequest x;
			x.parse("PUT /path HTTP/1.1\r\nkey: value\r\n\r\n", 34);
			REQUIRE(x.appendBody("bliblablub\r\n", 12) == false);
		}
		{
			// Invalid, because GET may not have a body
			HttpRequest x;
			x.parse("GET /path HTTP/1.1\r\nContent-Length: 5\r\nkey: value\r\n\r\n", 50);
			REQUIRE(x.appendBody("12345", 5) == false);
		}
		{
			// Invalid, because GET may not have a body
			HttpRequest x;
			x.parse("PUT /path HTTP/1.1\r\nContent-Length:blablabla\r\nkey: value\r\n\r\n", 57);
			REQUIRE(x.appendBody("12345", 5) == false);
		}
	}
	SECTION("Content Length Tests")
	{
		// Tests were deleted, as the new Body Reader handles bodies differently

		{
			// Invalid, because Content Length too low
			HttpRequest x;
			x.parse("PUT /path HTTP/1.1\r\nContent-Length:0\r\nkey: value\r\n\r\n", 50);
			REQUIRE(x.appendBody("12345", 5) == false);
			REQUIRE(errno == HTTP_BAD_REQUEST);
		}
		{
			// Invalid, because Content Length too low
			HttpRequest x;
			x.parse("PUT /path HTTP/1.1\r\nContent-Length: 2\r\nkey: value\r\n\r\n", 50);
			REQUIRE(x.appendBody("12345", 5) == false);
		}
		{
			// Invalid, because Content Length too low
			HttpRequest x;
			x.parse("PUT /path HTTP/1.1\r\nContent-Length: 5\r\nkey: value\r\n\r\n", 50);
			REQUIRE(x.appendBody("abcd", 4) == true);
			REQUIRE(x.appendBody("12345", 5) == false);
			REQUIRE(x.appendBody("x", 1) == true);
		}
		{ 
			// Invalid Content Length
			HttpRequest x;
			errno = 0;
			REQUIRE(x.parse("PUT / HTTP/1.1\r\nContent-Length:bla\r\nkey:value\r\n\r\n", 49) == false);
			REQUIRE(errno == HTTP_BAD_REQUEST);
		}
		{
			// Negative value for Content Length
			HttpRequest x;
			errno = 0;
			REQUIRE(x.parse("PUT / HTTP/1.1\r\nContent-Length: -1\r\nkey:value\r\n\r\n",  49) == false);
			REQUIRE(errno == HTTP_BAD_REQUEST);
		}
		{
			// MAX_INT Content Length
			HttpRequest x;
			errno = 0;
			REQUIRE(x.parse("PUT / HTTP/1.1\r\nContent-Length:2147483647\r\nkey:value\r\n\r\n", 56) == true);
			REQUIRE(errno == 0);
		}

	}
	SECTION("Valid Start Line")
	{
		// Testing methods
		{
			HttpRequest x;
			errno = 0;
			REQUIRE(x.parse("PUT /path HTTP/1.1\r\n", 20) == true);
			REQUIRE(x.getMethod() == "PUT");
			REQUIRE(x.getPath() == "/path");
			REQUIRE(x.getQuery() == "");
			REQUIRE(x.getHttpVer() == "HTTP/1.1");
			REQUIRE(x.getState() == HEADER);
			REQUIRE(errno == 0);
		}
		{
			HttpRequest x;
			errno = 0;
			REQUIRE(x.parse("PATCH / HTTP/1.1\r\n", 18) == true);
			REQUIRE(x.getMethod() == "PATCH");
			REQUIRE(x.getPath() == "/");
			REQUIRE(x.getQuery() == "");
			REQUIRE(x.getHttpVer() == "HTTP/1.1");
			REQUIRE(x.getState() == HEADER);
			REQUIRE(errno == 0);
		}
		{
			HttpRequest x;
			errno = 0;
			REQUIRE(x.parse("GET /root/dir HTTP/1.1\r\n", 24) == true);
			REQUIRE(x.getMethod() == "GET");
			REQUIRE(x.getPath() == "/root/dir");
			REQUIRE(x.getQuery() == "");
			REQUIRE(x.getHttpVer() == "HTTP/1.1");
			REQUIRE(x.getState() == HEADER);
			REQUIRE(errno == 0);
		}
		{
			HttpRequest x;
			errno = 0;
			REQUIRE(x.parse("DELETE /root/home HTTP/1.1\r\n", 28) == true);
			REQUIRE(x.getMethod() == "DELETE");
			REQUIRE(x.getPath() == "/root/home");
			REQUIRE(x.getQuery() == "");
			REQUIRE(x.getHttpVer() == "HTTP/1.1");
			REQUIRE(x.getState() == HEADER);
			REQUIRE(errno == 0);
		}
		{
			HttpRequest x;
			errno = 0;
			REQUIRE(x.parse("POST //|\\ HTTP/1.1\r\n", 20) == true);
			REQUIRE(x.getMethod() == "POST");
			REQUIRE(x.getPath() == "//|\\");
			REQUIRE(x.getQuery() == "");
			REQUIRE(x.getHttpVer() == "HTTP/1.1");
			REQUIRE(x.getState() == HEADER);
			REQUIRE(errno == 0);
		}
		{
			HttpRequest x;
			errno = 0;
			REQUIRE(x.parse("POST /index.html HTTP/1.1\r\n", 27) == true);
			REQUIRE(x.getMethod() == "POST");
			REQUIRE(x.getPath() == "/index.html");
			REQUIRE(x.getQuery() == "");
			REQUIRE(x.getHttpVer() == "HTTP/1.1");
			REQUIRE(x.getState() == HEADER);
			REQUIRE(errno == 0);
		}
		{
			HttpRequest x;
			errno = 0;
			REQUIRE(x.parse("POST /index.html?key=value HTTP/1.1\r\n", 37) == true);
			REQUIRE(x.getMethod() == "POST");
			REQUIRE(x.getPath() == "/index.html");
			REQUIRE(x.getQuery() == "key=value");
			REQUIRE(x.getHttpVer() == "HTTP/1.1");
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
			REQUIRE(x.parse("GET / HTTP/1.1\r\nkey:value\r\n", 28) == true);
			REQUIRE(errno == 0);
		}
		{
			HttpRequest x;
			errno = 0;
			REQUIRE(x.parse("GET / HTTP/1.1\r\nkey:    value\r\n", 31) == true);
			REQUIRE(errno == 0);
		}
		{
			HttpRequest x;
			errno = 0;
			REQUIRE(x.parse("GET / HTTP/1.1\r\nkey:value\r\nabc: 123\r\n\r\n", 39) == true);
			REQUIRE(errno == 0);
		}
	}
	SECTION("Valid Body Line")
	{
		// Testing Headers
		{
			HttpRequest x;
			errno = 0;
			REQUIRE(x.parse("PUT / HTTP/1.1\r\nContent-Length: 1\r\nkey:value\r\n\r\na", 49) == true);
			REQUIRE(errno == 0);
		}
		{
			HttpRequest x;
			errno = 0;
			REQUIRE(x.parse("PUT / HTTP/1.1\r\nContent-Length:10\r\nkey:value\r\n\r\nI am fish\n", 58) == true);
			REQUIRE(errno == 0);
		}
	}
	SECTION("Multi-call parsing")
	{
		// Testing Headers
		{
			HttpRequest x;
			std::string buffer = "";
			const char	*newData;
			size_t	newLength = 0;
			size_t	parseOffset = 0;
			errno = 0;

			buffer += "PUT /path ";
			parseOffset = x.getTotalBytesRead() - x.getTotalBytesHandled();
			newData = &((buffer.c_str())[parseOffset]);
			newLength = buffer.size() - parseOffset;
			REQUIRE(x.parse(newData, newLength) == true);
			REQUIRE(x.getTotalBytesRead() == buffer.size());
			REQUIRE(x.getBytesHandledLast() == 0);
			REQUIRE(x.getTotalBytesHandled() == 0);

			buffer += "HTTP/1.1\r\nkey:";
			parseOffset = x.getTotalBytesRead() - x.getTotalBytesHandled();
			newData = &((buffer.c_str())[parseOffset]);
			newLength = buffer.size() - parseOffset;
			REQUIRE(x.parse(newData, newLength) == true);
			REQUIRE(x.getTotalBytesRead() == 24);
			REQUIRE(x.getBytesHandledLast() == 20);
			REQUIRE(x.getTotalBytesHandled() == 20);
			
			buffer = buffer.substr(x.getBytesHandledLast());
			// Buffer is now only the rest that wasnt handled (key:)

			buffer += "value\r\n\r\n";
			parseOffset = x.getTotalBytesRead() - x.getTotalBytesHandled();
			newData = &((buffer.c_str())[parseOffset]);
			newLength = buffer.size() - parseOffset;
			REQUIRE(x.parse(newData, newLength) == true);
			REQUIRE(x.getTotalBytesRead() == 33);
			REQUIRE(x.getBytesHandledLast() == 13);
			REQUIRE(x.getState() == BODY);
			REQUIRE(errno == 0);
		}
	}
}
