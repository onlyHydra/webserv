#include <catch2/catch_all.hpp>
#include "PutPatchHandler.h"
#include "HttpRequest.h"
#include "HttpResponse.h"
#include "RequestContext.h"
#include "ServerConfig.h"
#include "HTTPCODES.h"
#include "ETagUtil.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>

/**
 * @brief Checks if a file has specified contents
 * @param length Amount of bytes the Content will be
 * @param filename Name of the file to check
 * @param goal The Content that the file should have
 * @warning length and goal have to match 
 */
static void	checkFileContent(int length, const std::string &filename, const std::string &goal)
{
		char buffer[length + 1];
		std::ifstream in;

		in.open(filename.c_str(), std::ios::in | std::ios::binary);
		REQUIRE(in.is_open());
		REQUIRE(in.good());
		memset((void *)&buffer, 0, length + 1);
		in.read(buffer, length);
		REQUIRE(in.gcount() == length);
		std::string content(buffer, length);
		REQUIRE(content == goal);
		REQUIRE(in.good());
		in.read(buffer, 1);
		REQUIRE(!in.good());
		REQUIRE(in.eof());
		in.close();
}

/**
 * Writes content to a file through replacing
 */
void writeToFile(const char *file, const std::string &content)
{
	std::ofstream stream;

	std::remove(file);
	stream.open(file, std::ios::out | std::ios::binary);
	if (stream.is_open())
	{
		stream.write(content.c_str(), content.length());
		stream.close();
	}
}

TEST_CASE("PUT_HANDLER ETag", "[handler][put][etag]")
{
	RequestContext ctx;
	PutPatchHandler pat;
	char cwd[4096];
	getcwd(cwd, sizeof(cwd));
	ctx.effective_root = std::string(cwd);
	ctx.rel_path = "/DeleteMe.txt";
	ctx.temp_filename = std::string(cwd) + "/DeleteMeBodyFile.txt";
	const char *bodyFile = ctx.temp_filename.c_str();
	std::string fileString = (ctx.effective_root + ctx.rel_path);
	const char *file = fileString.c_str();
	// file -> C string for the File to target
	// bodyFile -> C string for the file to hold the body information
	SECTION("Normal test with Content from body")
	{
		{
			HttpRequest req;
			HttpResponse res;
			std::string oldEtag;
			std::string newEtag;
			
			writeToFile(file, "123");
			oldEtag = ETagUtil::generate(file);
			std::string parse;
			parse = "PUT / HTTP/1.1\r\nContent-Length: 7\r\n\r\n";
			req.parse(parse.c_str(), parse.length());
			req.appendBody("Abc123\n", 7);
			REQUIRE(pat.handle(req, res, ctx) == true);
			REQUIRE(res.getStatusCode() == HTTP_OK);
			checkFileContent(7, file, "Abc123\n");
			newEtag = ETagUtil::generate(file);
			REQUIRE(res.headers.get(HDR_ETAG) != oldEtag);
			REQUIRE(res.headers.get(HDR_ETAG) == newEtag);
			std::remove(file);
		}
	}
	SECTION("PUT with valid If-Match Header")
	{
		{
			HttpRequest req;
			HttpResponse res;
			std::string oldEtag;
			std::string newEtag;
			
			writeToFile(file, "123");
			oldEtag = ETagUtil::generate(file);
			std::string parse;
			parse = "PUT / HTTP/1.1\r\nContent-Length: 7\r\nIf-Match: ";
			parse += oldEtag;
			parse += "\r\n\r\n";
			req.parse(parse.c_str(), parse.length());
			req.appendBody("Abc123\n", 7);
			REQUIRE(pat.handle(req, res, ctx) == true);
			REQUIRE(res.getStatusCode() == HTTP_OK);
			checkFileContent(7, file, "Abc123\n");
			newEtag = ETagUtil::generate(file);
			REQUIRE(res.headers.get(HDR_ETAG) != oldEtag);
			REQUIRE(res.headers.get(HDR_ETAG) == newEtag);
			std::remove(file);
		}
	}
	SECTION("PUT with invalid If-Match")
	{
		{
			HttpRequest req;
			HttpResponse res;
			
			writeToFile(file, "123");
			std::string parse;
			parse = "PUT / HTTP/1.1\r\nContent-Length: 7\r\nIf-Match: ";
			parse += "Invalid Etag";
			parse += "\r\n\r\n";
			req.parse(parse.c_str(), parse.length());
			req.appendBody("Abc123\n", 7);
			REQUIRE(pat.handle(req, res, ctx) == true);
			REQUIRE(res.getStatusCode() == HTTP_PRECON_FAIL);
			checkFileContent(3, file, "123");
			std::remove(file);
		}
	}
	SECTION("PUT with valid If-None-Match")
	{
		{
			HttpRequest req;
			HttpResponse res;
			std::string oldEtag;
			std::string newEtag;
			
			writeToFile(file, "123");
			oldEtag = ETagUtil::generate(file);
			std::string parse;
			parse = "PUT / HTTP/1.1\r\nContent-Length: 7\r\nIf-None-Match: ";
			parse += "Invalid Etag";
			parse += "\r\n\r\n";
			req.parse(parse.c_str(), parse.length());
			req.appendBody("Abc123\n", 7);
			REQUIRE(pat.handle(req, res, ctx) == true);
			REQUIRE(res.getStatusCode() == HTTP_OK);
			checkFileContent(7, file, "Abc123\n");
			newEtag = ETagUtil::generate(file);
			REQUIRE(res.headers.get(HDR_ETAG) != oldEtag);
			REQUIRE(res.headers.get(HDR_ETAG) == newEtag);
			std::remove(file);
		}
	}
	SECTION("PUT with invalid If-None-Match")
	{
		{
			HttpRequest req;
			HttpResponse res;
			std::string oldEtag;
			std::string newEtag;
			
			writeToFile(file, "123");
			oldEtag = ETagUtil::generate(file);
			std::string parse;
			parse = "PUT / HTTP/1.1\r\nContent-Length: 7\r\nIf-None-Match: ";
			parse += oldEtag;
			parse += "\r\n\r\n";
			req.parse(parse.c_str(), parse.length());
			req.appendBody("Abc123\n", 7);
			REQUIRE(pat.handle(req, res, ctx) == true);
			REQUIRE(res.getStatusCode() == HTTP_PRECON_FAIL);
			checkFileContent(3, file, "123");
			std::remove(file);
		}
	}
	SECTION("Checking for Hard ETag comparison")
	{
		{	// Comparing W/<etag> and <etag> should be a failed comparison
			// which should result in 412
			HttpRequest req;
			HttpResponse res;
			std::string oldEtag;
			std::string newEtag;
			
			writeToFile(file, "123");
			oldEtag = ETagUtil::generate(file);
			if (oldEtag.substr(0, 2) == "W/")
				oldEtag = oldEtag.substr(2, oldEtag.length() - 2);
			else
				oldEtag = std::string("W/") + oldEtag;
			std::string parse;
			parse = "PUT / HTTP/1.1\r\nContent-Length: 7\r\nIf-Match: ";
			parse += oldEtag;
			parse += "\r\n\r\n";
			req.parse(parse.c_str(), parse.length());
			req.appendBody("Abc123\n", 7);
			REQUIRE(pat.handle(req, res, ctx) == true);
			REQUIRE(res.getStatusCode() == HTTP_PRECON_FAIL);
			checkFileContent(3, file, "123");
			std::remove(file);
		}
	}
}