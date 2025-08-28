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
 * @brief Checks if a file has specified content
 * @param length Amount of bytes the Content will be
 * @param filename Name of the file to check
 * @param goal The Content that the file should have
 * @warning length and goal have to match 
 */
void	checkFileContent(int length, const std::string &filename, const std::string &goal)
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
	char cwd[4096];
	getcwd(cwd, sizeof(cwd));
	std::string directory(cwd);
	std::string path(cwd);
	std::string bodyFile(cwd);
	path += "/DeleteMe.txt";
	bodyFile += "/DeleteMeBodyFile.txt";
	SECTION("Normal test with Content from body")
	{
		{	// Put new file with some content
			HttpRequest req;
			HttpResponse res;
			RequestContext ctx;
			
			std::remove(path.c_str());
			std::string parse;
			parse = "PUT " + path + " HttpVersion/1.1\r\nContent-Length: 7\r\n\r\nAbc123\n";
			req.parse(parse.c_str(), parse.length());
			REQUIRE(PutPatchHandler::handle_put(req, res, ctx) == HTTP_FILE_CREATED);
			checkFileContent(7, path, "Abc123\n");
			std::remove(path.c_str());
		}
		{	// Put new file with less content
			HttpRequest req;
			HttpResponse res;
			RequestContext ctx;

			std::remove(path.c_str());
			std::string parse;
			parse = "PUT " + path + " HttpVersion/1.1\r\nContent-Length: 1\r\n\r\nX";
			req.parse(parse.c_str(), parse.length());
			REQUIRE(PutPatchHandler::handle_put(req, res, ctx) == HTTP_FILE_CREATED);
			checkFileContent(1, path, "X");
			std::remove(path.c_str());
		}
		{	// Put existing file with less content
			HttpRequest req;
			HttpResponse res;
			RequestContext ctx;
			std::ofstream file;

			file.open(path, std::ios::out | std::ios::binary);
			if (file.is_open())
			{
				file.write("123", 3);
				file.close();
			}
			std::string parse;
			parse = "PUT " + path + " HttpVersion/1.1\r\nContent-Length: 1\r\n\r\nX";
			req.parse(parse.c_str(), parse.length());
			REQUIRE(PutPatchHandler::handle_put(req, res, ctx) == HTTP_OK);
			checkFileContent(1, path, "X");
			std::remove(path.c_str());
		}
	}
	SECTION("Normal test with Content from temp file")
	{
		{	// Put new file with some content which comes from temp body file
			HttpRequest req;
			HttpResponse res;
			RequestContext ctx;
			std::ofstream file;
			
			ctx.temp_file_used = true;
			ctx.temp_filename = bodyFile;
			std::remove(bodyFile.c_str());
			std::remove(path.c_str());
			file.open(bodyFile, std::ios::out | std::ios::binary);
			if (file.is_open())
			{
				file.write("CONTENT", 7);
				file.close();
			}
			std::string parse;
			parse = "PUT " + path + " HttpVersion/1.1\r\nContent-Length: 7\r\n\r\n";
			req.parse(parse.c_str(), parse.length());
			REQUIRE(PutPatchHandler::handle_put(req, res, ctx) == HTTP_FILE_CREATED);
			checkFileContent(7, path, "CONTENT");
			std::remove(bodyFile.c_str());
			std::remove(path.c_str());
		}
	}
	SECTION("Invalid test for writing")
	{
		{	// Invalid PUT on target with wrong permissions
			HttpRequest req;
			HttpResponse res;
			RequestContext ctx;
			std::string wrongAccess;
			std::ofstream file;

			wrongAccess = path + "invalid.txt";
			file.open(wrongAccess, std::ios::out | std::ios::binary);
			if (file.is_open())
			{
				file.write("untouchable", 11);
				file.close();
			}
			chmod(wrongAccess.c_str(), S_IRUSR | S_IRGRP | S_IROTH);
			std::string parse;
			parse = "PUT " + wrongAccess + " HttpVersion/1.1\r\nContent-Length: 7\r\n\r\nAbc123\n";
			req.parse(parse.c_str(), parse.length());
			REQUIRE(PutPatchHandler::handle_put(req, res, ctx) == HTTP_FORBIDDEN);
			std::remove(wrongAccess.c_str());
		}
	}
	SECTION("Invalid test for reading")
	{
		{	// Invalid PUT on target from temp file with wrong permissions
			HttpRequest req;
			HttpResponse res;
			RequestContext ctx;
			std::ofstream file;
			
			ctx.temp_file_used = true;
			ctx.temp_filename = bodyFile;
			std::remove(bodyFile.c_str());
			std::remove(path.c_str());
			file.open(bodyFile, std::ios::out | std::ios::binary);
			if (file.is_open())
			{
				file.write("CONTENT", 7);
				file.close();
			}
			chmod(bodyFile.c_str(), 0);
			std::string parse;
			parse = "PUT " + path + " HttpVersion/1.1\r\nContent-Length: 7\r\n\r\n";
			req.parse(parse.c_str(), parse.length());
			REQUIRE(PutPatchHandler::handle_put(req, res, ctx) == HTTP_FORBIDDEN);
			std::remove(bodyFile.c_str());
			std::remove(path.c_str());
		}
	}
}

TEST_CASE("PATCH HANDLER", "[handler][patch]")
{
	RequestContext ctx;
	ServerConfig cfg;
	char cwd[4096];
	getcwd(cwd, sizeof(cwd));
	std::string directory(cwd);
	std::string path(cwd);
	std::string bodyFile(cwd);
	std::string configFile(directory += "/config/extended.conf");
	path += "/DeleteMe.txt";
	bodyFile += "/DeleteMeBodyFile.txt";
	cfg.parseFile(configFile);
	ctx.cfg = &cfg;
	SECTION("Normal test with Content from body, append")
	{
		{	// Patch append file based on body
			HttpRequest req;
			HttpResponse res;
			std::ofstream pre;
			std::string parse;
			
			std::remove(path.c_str());
			pre.open(path, std::ios::out | std::ios::binary);
			if (pre.is_open())
			{
				pre.write("OLD", 3);
				pre.close();
			}
			parse = "PATCH " + path + " HttpVersion/1.1\r\n" + \
			"Content-Length: 7\r\n" + \
			"Content-Type: application/vnd.webserv.append\r\n" + \
			"\r\n" + \
			"xXNEWXx";
			req.parse(parse.c_str(), parse.length());
			REQUIRE(PutPatchHandler::handle_patch(req, res, ctx) == HTTP_OK);	
			checkFileContent(10, path, "OLDxXNEWXx");
			std::remove(path.c_str());
		}
	}
	SECTION("Normal test with Content from body, overwrite")
	{
		{	// Patch overwrite file based on body
			HttpRequest req;
			HttpResponse res;
			std::ofstream pre;
			std::string parse;
			
			std::remove(path.c_str());
			pre.open(path, std::ios::out | std::ios::binary);
			if (pre.is_open())
			{
				pre.write("OLD", 3);
				pre.close();
			}
			parse = "PATCH " + path + " HttpVersion/1.1\r\n" + \
			"Content-Length: 7\r\n" + \
			HDR_PATCH_OFFSET + ": 2\r\n" +\
			"Content-Type: application/vnd.webserv.overwrite\r\n" + \
			"\r\n" + \
			"xXNEWXx";
			req.parse(parse.c_str(), parse.length());
			REQUIRE(PutPatchHandler::handle_patch(req, res, ctx) == HTTP_OK);	
			checkFileContent(9, path, "OLxXNEWXx");
			std::remove(path.c_str());
		}
	}
	SECTION("Normal test with Content from body, overwrite, with big ass offset")
	{
		{	// Patch overwrite file based on body
			HttpRequest req;
			HttpResponse res;
			std::ofstream pre;
			std::string parse;
			
			std::remove(path.c_str());
			pre.open(path, std::ios::out | std::ios::binary);
			if (pre.is_open())
			{
				pre.write("OLD", 3);
				pre.close();
			}
			parse = "PATCH " + path + " HttpVersion/1.1\r\n" + \
			"Content-Length: 7\r\n" + \
			HDR_PATCH_OFFSET + ": 999999999\r\n" +\
			"Content-Type: application/vnd.webserv.overwrite\r\n" + \
			"\r\n" + \
			"xXNEWXx";
			req.parse(parse.c_str(), parse.length());
			REQUIRE(PutPatchHandler::handle_patch(req, res, ctx) == HTTP_OK);	
			checkFileContent(10, path, "OLDxXNEWXx");
			std::remove(path.c_str());
		}
	}
	SECTION("Normal test with Content from body, overwrite, with no offset")
	{
		{	// Patch overwrite file based on body
			HttpRequest req;
			HttpResponse res;
			std::ofstream pre;
			std::string parse;
			
			std::remove(path.c_str());
			pre.open(path, std::ios::out | std::ios::binary);
			if (pre.is_open())
			{
				pre.write("OLD", 3);
				pre.close();
			}
			parse = "PATCH " + path + " HttpVersion/1.1\r\n" + \
			"Content-Length: 7\r\n" + \
			"Content-Type: application/vnd.webserv.overwrite\r\n" + \
			"\r\n" + \
			"xXNEWXx";
			req.parse(parse.c_str(), parse.length());
			REQUIRE(PutPatchHandler::handle_patch(req, res, ctx) == HTTP_OK);	
			checkFileContent(7, path, "xXNEWXx");
			std::remove(path.c_str());
		}
	}
	SECTION("Normal test with Content from temp file, append")
	{
		ctx.temp_file_used = true;
		ctx.temp_filename = bodyFile;
		{	// Patch append file based on temp file
			HttpRequest req;
			HttpResponse res;
			std::ofstream pre;
			std::string parse;
			std::ofstream file;
			
			std::remove(bodyFile.c_str());
			std::remove(path.c_str());
			file.open(bodyFile, std::ios::out | std::ios::binary);
			if (file.is_open())
			{
				file.write("xXNEWXx", 7);
				file.close();
			}
			pre.open(path, std::ios::out | std::ios::binary);
			if (pre.is_open())
			{
				pre.write("OLD", 3);
				pre.close();
			}
			parse = "PATCH " + path + " HttpVersion/1.1\r\n" + \
			"Content-Length: 7\r\n" + \
			"Content-Type: application/vnd.webserv.append\r\n" + \
			"\r\n";
			req.parse(parse.c_str(), parse.length());
			REQUIRE(PutPatchHandler::handle_patch(req, res, ctx) == HTTP_OK);	
			checkFileContent(10, path, "OLDxXNEWXx");
			std::remove(path.c_str());
			std::remove(bodyFile.c_str());
		}
		ctx.temp_file_used = false;
		ctx.temp_filename = "";
	}
	SECTION("Invalid test for Accepted-Patch methods and missing Content Type")
	{
		{	// Checking Responses Allowed-Types Header Entry and missing Content-Type (patch method)
			HttpRequest req;
			HttpResponse res;
			std::string parse;
			std::ofstream pre;
			
			std::remove(path.c_str());
			pre.open(path, std::ios::out | std::ios::binary);
			if (pre.is_open())
			{
				pre.write("OLD", 3);
				pre.close();
			}
			parse = "PATCH " + path + " HttpVersion/1.1\r\n" + \
			"Content-Length: 7\r\n" + \
			"\r\n" + \
			"xXNEWXx";
			req.parse(parse.c_str(), parse.length());
			REQUIRE(PutPatchHandler::handle_patch(req, res, ctx) == HTTP_INV_MEDIA);
			// Carefull! The order of theese will depend on the order of the extended.conf Mime types
			REQUIRE(res.headers.get(HDR_ACCEPT_PATCH) == "application/vnd.webserv.append, application/vnd.webserv.overwrite");
			checkFileContent(3, path, "OLD");
			std::remove(path.c_str());
		}
	}
	SECTION("Invalid test for invalid method")
	{
		{	// Checking Forbidden / Invalid / Unknown Patch method
			HttpRequest req;
			HttpResponse res;
			std::string parse;
			std::ofstream pre;
			
			std::remove(path.c_str());
			pre.open(path, std::ios::out | std::ios::binary);
			if (pre.is_open())
			{
				pre.write("OLD", 3);
				pre.close();
			}
			parse = "PATCH " + path + " HttpVersion/1.1\r\n" + \
			"Content-Length: 7\r\n" + \
			"Content-Type: application/vnd.webserv.bogus\r\n" + \
			"\r\n" + \
			"xXNEWXx";
			req.parse(parse.c_str(), parse.length());
			REQUIRE(PutPatchHandler::handle_patch(req, res, ctx) == HTTP_INV_MEDIA);
			checkFileContent(3, path, "OLD");
			std::remove(path.c_str());
		}
	}
}