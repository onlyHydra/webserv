#include <catch2/catch_all.hpp>
#include <sstream>
#include "ReturnHandler.h"
#include "HttpResponse.h"
#include "HTTPCODES.h"
#include "HEADER_ENTRIES.h"
#include <sys/wait.h>
#include <iostream>

std::string resToStr(const HttpResponse &res)
{
	std::ostringstream out;
	out << res;
	return (out.str());
}

HttpResponse defaultRes(int code)
{
	HttpResponse r;
	r.ensureDefaultHeaders();
	r.setStatus(code);
	return(r);
}

TEST_CASE("Return Directive", "[return][handler]")
{
	SECTION("200 - Get Text back")
	{
		int code = 200;
		std::string text = "123Hello";

		HttpResponse res = defaultRes(code);
		res.headers.set(HDR_CONTENT_TYPE, "text/plain; charset=utf-8");
		res.setBody(text);
		REQUIRE(resToStr(res) == ReturnHandler::handle(code, text));
	}
	SECTION("204 - Get (basically) empty 204 response")
	{
		int code = 204;
		std::string text = "123Hello";

		HttpResponse res = defaultRes(code);
		REQUIRE(resToStr(res) == ReturnHandler::handle(code, text));
	}
	SECTION("301 - Get redirected")
	{
		int code = 301;
		std::string text = "/newPath";

		HttpResponse res = defaultRes(code);
		res.headers.set(HDR_LOCATION, text);
		REQUIRE(resToStr(res) == ReturnHandler::handle(code, text));
	}
	SECTION("302 - Get redirected")
	{
		int code = 302;
		std::string text = "/newPath";

		HttpResponse res = defaultRes(code);
		res.headers.set(HDR_LOCATION, text);
		REQUIRE(resToStr(res) == ReturnHandler::handle(code, text));
	}
	SECTION("307 - Get redirected")
	{
		int code = 307;
		std::string text = "/newPath";

		HttpResponse res = defaultRes(code);
		res.headers.set(HDR_LOCATION, text);
		REQUIRE(resToStr(res) == ReturnHandler::handle(code, text));
	}
	SECTION("308 - Get redirected")
	{
		int code = 308;
		std::string text = "/newPath";

		HttpResponse res = defaultRes(code);
		res.headers.set(HDR_LOCATION, text);
		REQUIRE(resToStr(res) == ReturnHandler::handle(code, text));
	}
	SECTION("Shell script")
	{
		// This will run the shell script for theese test cases
		// The shell script will compile our main, if it doesnt exist
		// It will then use a free port and its own config
		// So it will be pretty tough to mess up, unless the actual test case fails
		pid_t pid;

		pid = fork();
		if (pid == 0)
		{
			int fd = open("/dev/null", O_WRONLY);
			dup2(fd, STDOUT_FILENO);
			execl("./tests/return_directive.sh", "./tests/return_directive.sh", NULL);
			exit(0);
		}
		int status;
		waitpid(-1, &status, 0);
		REQUIRE(WEXITSTATUS(status) == 0);
	}
}