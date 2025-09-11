#include <catch2/catch_all.hpp>
#include "PutPatchHandler.h"
#include "HttpRequest.h"
#include "HttpResponse.h"
#include "RequestContext.h"
#include "ServerConfig.h"
#include "HTTPCODES.h"
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

TEST_CASE("PUT_HANDLER", "[handler][put]")
{
	// Prepare ctx.effective root to be root of folder and ctx.rel_path to be filename
	// Also prepare a file to contain the bodys information
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
	SECTION("Normal test with Content from body")
	{
		{	// Put new file with some content
			HttpRequest req;
			HttpResponse res;
			
			std::remove(file);
			std::string parse;
			parse = "PUT / HttpVersion/1.1\r\nContent-Length: 7\r\n\r\nAbc123\n";
			req.parse(parse.c_str(), parse.length());
			REQUIRE(pat.handle(req, res, ctx) == true);
			REQUIRE(res.getStatusCode() == HTTP_FILE_CREATED);
			checkFileContent(7, file, "Abc123\n");
			std::remove(file);
		}
		{	// Put new file with less content
			HttpRequest req;
			HttpResponse res;

			std::remove(file);
			std::string parse;
			parse = "PUT / HttpVersion/1.1\r\nContent-Length: 1\r\n\r\nX";
			req.parse(parse.c_str(), parse.length());
			REQUIRE(pat.handle(req, res, ctx) == true);
			REQUIRE(res.getStatusCode()  == HTTP_FILE_CREATED);
			checkFileContent(1, file, "X");
			std::remove(file);
		}
		{	// Put existing file with less content
			HttpRequest req;
			HttpResponse res;
			std::ofstream stream;

			stream.open(file, std::ios::out | std::ios::binary);
			if (stream.is_open())
			{
				stream.write("123", 3);
				stream.close();
			}
			std::string parse;
			parse = "PUT / HttpVersion/1.1\r\nContent-Length: 1\r\n\r\nX";
			req.parse(parse.c_str(), parse.length());
			REQUIRE(pat.handle(req, res, ctx) == true);
			REQUIRE(res.getStatusCode() == HTTP_OK);
			checkFileContent(1, file, "X");
			std::remove(file);
		}
	}
	SECTION("Normal test with Content from temp file")
	{
		{	// Put new file with some content which comes from temp body file
			HttpRequest req;
			HttpResponse res;
			std::ofstream stream;
			
			ctx.temp_file_used = true;
			std::remove(ctx.temp_filename.c_str());
			std::remove(file);
			stream.open(bodyFile, std::ios::out | std::ios::binary);
			if (stream.is_open())
			{
				stream.write("CONTENT", 7);
				stream.close();
			}
			std::string parse;
			parse = "PUT / HttpVersion/1.1\r\nContent-Length: 7\r\n\r\n";
			req.parse(parse.c_str(), parse.length());
			REQUIRE(pat.handle(req, res, ctx) == true);
			REQUIRE(res.getStatusCode() == HTTP_FILE_CREATED);
			checkFileContent(7, file, "CONTENT");
			std::remove(bodyFile);
			std::remove(file);
		}
	}
	SECTION("Invalid test for writing")
	{
		{	// Invalid PUT on target with wrong permissions
			HttpRequest req;
			HttpResponse res;
			std::ofstream stream;

			std::remove(file);
			stream.open(file, std::ios::out | std::ios::binary);
			if (stream.is_open())
			{
				stream.write("untouchable", 11);
				stream.close();
			}
			chmod(file, S_IRUSR | S_IRGRP | S_IROTH);
			std::string parse;
			parse = "PUT / HttpVersion/1.1\r\nContent-Length: 7\r\n\r\nAbc123\n";
			req.parse(parse.c_str(), parse.length());
			REQUIRE(pat.handle(req, res, ctx) == false);
			REQUIRE(res.getStatusCode() == HTTP_FORBIDDEN);
			std::remove(file);
		}
	}
	SECTION("Invalid test for reading")
	{
		{	// Invalid PUT on target from temp file with wrong permissions
			HttpRequest req;
			HttpResponse res;
			std::ofstream stream;
			
			ctx.temp_file_used = true;
			std::remove(bodyFile);
			std::remove(file);
			stream.open(bodyFile, std::ios::out | std::ios::binary);
			if (stream.is_open())
			{
				stream.write("CONTENT", 7);
				stream.close();
			}
			chmod(bodyFile, 0);
			std::string parse;
			parse = "PUT / HttpVersion/1.1\r\nContent-Length: 7\r\n\r\n";
			req.parse(parse.c_str(), parse.length());
			REQUIRE(pat.handle(req, res, ctx) == false);
			REQUIRE(res.getStatusCode() == HTTP_FORBIDDEN);
			std::remove(bodyFile);
			std::remove(file);
		}
	}
}

TEST_CASE("PATCH HANDLER", "[handler][patch]")
{
	RequestContext ctx;
	PutPatchHandler pat;
	char cwd[4096];
	getcwd(cwd, sizeof(cwd));
	ctx.effective_root = std::string(cwd);
	ctx.rel_path = "/DeleteMe.txt";
	ctx.temp_filename = std::string(cwd) + "/DeleteMeBodyFile.txt";
	const char *bodyFile = ctx.temp_filename.c_str();
	std::string fileString = ctx.effective_root + ctx.rel_path;
	const char *file = fileString.c_str();
	ServerConfig cfg;
	cfg.parseFile(ctx.effective_root + "/config/extended.conf");
	ctx.cfg = &cfg;
	SECTION("Normal test with Content from body, append")
	{
		{	// Patch append file based on body
			HttpRequest req;
			HttpResponse res;
			std::ofstream pre;
			std::string parse;
			
			std::remove(file);
			pre.open(file, std::ios::out | std::ios::binary);
			if (pre.is_open())
			{
				pre.write("OLD", 3);
				pre.close();
			}
			parse = "PATCH / HttpVersion/1.1\r\nContent-Length: 7\r\nContent-Type: application/vnd.webserv.append\r\n\r\nxXNEWXx";
			req.parse(parse.c_str(), parse.length());
			REQUIRE(pat.handle(req, res, ctx) == true);
			REQUIRE(res.getStatusCode() == HTTP_OK);
			checkFileContent(10, file, "OLDxXNEWXx");
			std::remove(file);
		}
	}
	SECTION("Normal test with Content from body, overwrite")
	{
		{	// Patch overwrite file based on body
			HttpRequest req;
			HttpResponse res;
			std::ofstream pre;
			std::string parse;
			
			std::remove(file);
			pre.open(file, std::ios::out | std::ios::binary);
			if (pre.is_open())
			{
				pre.write("OLD", 3);
				pre.close();
			}
			parse = "PATCH / HttpVersion/1.1\r\nContent-Length: 7\r\n" + std::string(HDR_PATCH_OFFSET) + ": 2\r\nContent-Type: application/vnd.webserv.overwrite\r\n\r\nxXNEWXx";
			req.parse(parse.c_str(), parse.length());
			REQUIRE(pat.handle(req, res, ctx) == true);
			REQUIRE(res.getStatusCode() == HTTP_OK);
			checkFileContent(9, file, "OLxXNEWXx");
			std::remove(file);
		}
	}
	SECTION("Normal test with Content from body, overwrite, with big ass offset")
	{
		{	// Patch overwrite file based on body
			HttpRequest req;
			HttpResponse res;
			std::ofstream pre;
			std::string parse;
			
			std::remove(file);
			pre.open(file, std::ios::out | std::ios::binary);
			if (pre.is_open())
			{
				pre.write("OLD", 3);
				pre.close();
			}
			parse = "PATCH / HttpVersion/1.1\r\n" \
			"Content-Length: 7\r\n" + \
			std::string(HDR_PATCH_OFFSET) + ": 999999999\r\n" +\
			"Content-Type: application/vnd.webserv.overwrite\r\n" + \
			"\r\n" + \
			"xXNEWXx";
			req.parse(parse.c_str(), parse.length());
			REQUIRE(pat.handle(req, res, ctx) == true);
			REQUIRE(res.getStatusCode() == HTTP_OK);
			checkFileContent(10, file, "OLDxXNEWXx");
			std::remove(file);
		}
	}
	SECTION("Normal test with Content from body, overwrite, with no offset")
	{
		{	// Patch overwrite file based on body
			HttpRequest req;
			HttpResponse res;
			std::ofstream pre;
			std::string parse;
			
			std::remove(file);
			pre.open(file, std::ios::out | std::ios::binary);
			if (pre.is_open())
			{
				pre.write("OLD", 3);
				pre.close();
			}
			parse = std::string("PATCH / HttpVersion/1.1\r\n") + \
			"Content-Length: 7\r\n" + \
			"Content-Type: application/vnd.webserv.overwrite\r\n" + \
			"\r\n" + \
			"xXNEWXx";
			req.parse(parse.c_str(), parse.length());
			REQUIRE(pat.handle(req, res, ctx) == true);
			REQUIRE(res.getStatusCode() == HTTP_OK);
			checkFileContent(7, file, "xXNEWXx");
			std::remove(file);
		}
	}
	SECTION("Normal test with Content from temp file, append")
	{
		ctx.temp_file_used = true;
		{	// Patch append file based on temp file
			HttpRequest req;
			HttpResponse res;
			std::ofstream pre;
			std::string parse;
			std::ofstream bodyStream;
			
			std::remove(bodyFile);
			std::remove(file);
			bodyStream.open(bodyFile, std::ios::out | std::ios::binary);
			if (bodyStream.is_open())
			{
				bodyStream.write("xXNEWXx", 7);
				bodyStream.close();
			}
			pre.open(file, std::ios::out | std::ios::binary);
			if (pre.is_open())
			{
				pre.write("OLD", 3);
				pre.close();
			}
			parse = std::string("PATCH / HttpVersion/1.1\r\n") + \
			"Content-Length: 7\r\n" + \
			"Content-Type: application/vnd.webserv.append\r\n" + \
			"\r\n";
			req.parse(parse.c_str(), parse.length());
			REQUIRE(pat.handle(req, res, ctx) == true);
			REQUIRE(res.getStatusCode() == HTTP_OK);	
			checkFileContent(10, file, "OLDxXNEWXx");
			std::remove(file);
			std::remove(bodyFile);
		}
		ctx.temp_file_used = false;
	}
	SECTION("Test for Accepted-Patch methods and missing Content Type")
	{
		{	// Checking Responses Allowed-Types Header Entry and missing Content-Type (patch method)
			HttpRequest req;
			HttpResponse res;
			std::string parse;
			std::ofstream pre;
			
			std::remove(file);
			pre.open(file, std::ios::out | std::ios::binary);
			if (pre.is_open())
			{
				pre.write("OLD", 3);
				pre.close();
			}
			parse = std::string("PATCH / HttpVersion/1.1\r\n") + \
			"Content-Length: 7\r\n" + \
			"\r\n" + \
			"xXNEWXx";
			req.parse(parse.c_str(), parse.length());
			REQUIRE(pat.handle(req, res, ctx) == false);
			REQUIRE(res.getStatusCode() == HTTP_INV_MEDIA);
			// Carefull! The order of theese will depend on the order of the extended.conf Mime types
			REQUIRE(res.headers.get(HDR_ACCEPT_PATCH) == "application/vnd.webserv.append, application/vnd.webserv.overwrite");
			checkFileContent(3, file, "OLD");
			std::remove(file);
		}
	}
	SECTION("Test for invalid method")
	{
		{	// Checking Forbidden / Invalid / Unknown Patch method
			HttpRequest req;
			HttpResponse res;
			std::string parse;
			std::ofstream pre;
			
			std::remove(file);
			pre.open(file, std::ios::out | std::ios::binary);
			if (pre.is_open())
			{
				pre.write("OLD", 3);
				pre.close();
			}
			parse = std::string("PATCH / HttpVersion/1.1\r\n") + \
			"Content-Length: 7\r\n" + \
			"Content-Type: application/vnd.webserv.bogus\r\n" + \
			"\r\n" + \
			"xXNEWXx";
			req.parse(parse.c_str(), parse.length());
			REQUIRE(pat.handle(req, res, ctx) == false);
			REQUIRE(res.getStatusCode() == HTTP_INV_MEDIA);	
			checkFileContent(3, file, "OLD");
			std::remove(file);
		}
	}
}