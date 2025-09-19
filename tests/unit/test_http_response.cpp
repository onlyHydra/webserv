#include <catch2/catch_all.hpp>
#include "HttpResponse.h"
#include <sstream>
#include <iostream>
#include "HEADER_ENTRIES.h"  // for HDR_CONTENT_LENGTH / HDR_CONTENT_TYPE (if you want the optional header checks)

// Function stolen from Response, cause I am too lazy to understand how to make this file access that function
static std::string rfc1123Now()
{
	char buf[64];
	std::time_t t = std::time(0);
	std::tm gmt;
#if defined(_WIN32)
	gmtime_s(&gmt, &t);
#else
	gmt = *std::gmtime(&t);
#endif
	std::strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", &gmt);
	return std::string(buf);
}

TEST_CASE("HttpResponse basic API", "[HttpResponse]") {
    HttpResponse r;

    SECTION("Default construction is sane") {
        // We don’t assert on defaults that vary between versions (e.g., http_version),
        // only that object is usable and starts with empty body.
        REQUIRE(r.body.size() == 0);
        REQUIRE(r.bodyLength == 0);
    }

    SECTION("setStatus / getters round-trip") {
        r.setStatus(404, "Not Found");
        REQUIRE(r.getStatusCode() == 404);
        REQUIRE(r.getReason() == std::string("Not Found"));

        r.setStatus(500, "Internal Server Error");
        REQUIRE(r.getStatusCode() == 500);
        REQUIRE(r.getReason() == std::string("Internal Server Error"));
    }

    SECTION("clearBody empties body and sets Content-Length: 0") {
        // Seed some body and bodyLength to ensure clearBody() resets both.
        const char msg[] = "hello";
        r.body.assign(msg, msg + sizeof(msg) - 1);
        r.bodyLength = r.body.size();

        r.clearBody();

        REQUIRE(r.body.empty());
        REQUIRE(r.bodyLength == 0);

        // Optional: verify Content-Length header set by clearBody()
        REQUIRE(r.headers.get(HDR_CONTENT_LENGTH) == std::string("0"));
    }

    SECTION("Manual body assignment updates bodyLength when you set it") {
        // This mirrors how the pipeline typically finalizes Content-Length & bodyLength.
        const char msg[] = "hello";
        r.body.assign(msg, msg + sizeof(msg) - 1);
        r.bodyLength = r.body.size();

        REQUIRE(r.body.size() == 5);
        REQUIRE(r.bodyLength == 5);
    }

    SECTION("Headers container is usable") {
        // Not asserting on defaults — just verify setters/getters.
        r.headers.set(HDR_CONTENT_TYPE, "text/plain");
        REQUIRE(r.headers.get(HDR_CONTENT_TYPE) == std::string("text/plain"));
    }
}

TEST_CASE("HttpResponse Serializing", "[HttpResponse][serialize]")
{
	SECTION("Minimal default response")
	{
		HttpResponse res;
		std::ostringstream out;
		std::string result;

		out << res;
		result = out.str();
		REQUIRE(result == "HTTP/1.1 200 OK\r\n\r\n");
	}
	SECTION("Custom Response")
	{
		HttpResponse res;
		std::ostringstream out;
		std::string result;

		// Setting attributes
		res.status = 8080;
		res.reason = "whatever";
		res.http_version = "https/0.1";
		res.headers.set("random_key", "some_value");

		out << res;
		result = out.str();
		REQUIRE(result == "https/0.1 8080 whatever\r\nrandom_key: some_value\r\n\r\n");
	}
	SECTION("Headers and CookieJar")
	{
		HttpRequest req;
		HttpResponse res(req);
		std::ostringstream out;
		std::string result;
		std::string goal;

		std::string parse = "GET / HTTP/1.1\r\nCookie: key1=value1; key2=value2\r\n\r\n";
		req.parse(parse.c_str(), parse.size());
		res.ensureDefaultHeaders();
		out << res;
		result = out.str();
		goal = "HTTP/1.1 200 OK\r\n";
		goal += "Set-Cookie: key1=value1\r\n";
		goal += "Set-Cookie: key2=value2\r\n";
		goal += "Connection: close\r\n";
		goal += "Content-Length: 0\r\n";
		goal += "Date: ";
		goal += rfc1123Now();
		goal += "\r\n";
		goal += "Server: webserv\r\n\r\n";
		(void)goal;
		// If this breaks, you should check if the Default Headers still match
		// ./test_webserv "[HttpResponse][serialize]"
		// std::cout << "Result:\n{\n" << result << "}\nExpected:\n{" << goal << "}" << std::endl;
		REQUIRE(result == goal);
	}
}
