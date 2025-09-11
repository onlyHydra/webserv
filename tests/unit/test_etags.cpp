#include <catch2/catch_all.hpp>
#include <unistd.h>
#include <iostream>
#include <fstream>
#include "ETagUtil.h"

static void writeToFile(const char *file, const std::string &content)
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

TEST_CASE("Etag tests", "[etag][lel]")
{
	char cwd[4096];
	getcwd(cwd, sizeof(cwd));
	std::string fileString = std::string(cwd) + std::string("DeleteMe.txt");
	const char *file = fileString.c_str();
	// file -> C string for the File to target
	SECTION("Normal test with Content from body")
	{
		std::string oldEtag;
		std::string newEtag;

		writeToFile(file, "blablabla");
		oldEtag = ETagUtil::generate(file);
		REQUIRE(!oldEtag.empty());
		newEtag = ETagUtil::generate(file);
		REQUIRE(!newEtag.empty());
		REQUIRE(oldEtag == newEtag);
		usleep(100000);
		writeToFile(file, "blib blob");
		newEtag = ETagUtil::generate(file);
		std::cout << (oldEtag == newEtag) << std::endl;
		std::cout << (ETagUtil::weakComp(oldEtag, newEtag)) << std::endl;
		REQUIRE(!ETagUtil::weakComp(oldEtag, newEtag));
		REQUIRE(!ETagUtil::strongComp(oldEtag, newEtag));
	}
	SECTION("Comparing functions for ETags")
	{
		std::string weak1 = "W/\"abc\"";
		std::string weak2 = "W/\"123\"";
		std::string strong1 = "\"abc\"";
		std::string strong2 ="\"123\"";

		// Weak Comparison (ignores W/ in front)
		REQUIRE(ETagUtil::weakComp(weak1, strong1));
		REQUIRE(ETagUtil::weakComp(weak2, strong2));
		REQUIRE(!ETagUtil::weakComp(weak1, strong2));
		REQUIRE(!ETagUtil::weakComp(weak2, strong1));
		REQUIRE(!ETagUtil::weakComp(weak1, weak2));
		REQUIRE(!ETagUtil::weakComp(strong1, strong2));

		// Strong Comparison (need to be exaxctly equal)
		REQUIRE(!ETagUtil::strongComp(weak1, strong1));
		REQUIRE(!ETagUtil::strongComp(weak2, strong2));
		REQUIRE(!ETagUtil::strongComp(weak1, strong2));
		REQUIRE(!ETagUtil::strongComp(weak2, strong1));
		REQUIRE(!ETagUtil::strongComp(weak1, weak2));
		REQUIRE(!ETagUtil::strongComp(strong1, strong2));
		REQUIRE(ETagUtil::strongComp("abc", "abc"));
	}
}