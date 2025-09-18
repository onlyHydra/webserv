#include <catch2/catch_all.hpp>
#include <iostream>
#include <fstream>
#include <ostream>
#include "Headers.h"
#include "CookieJar.h"
#include "HttpResponse.h"
#include "HttpRequest.h"
#include <ostream>	

TEST_CASE("Header tests", "[http][header]")
{
	SECTION("Unset Header")
	{
		std::ofstream out;
		// Check everything on unset Header
		Headers test;
		Headers empty;

		test.getBegin();
		test.getEnd();
		out << test;
		REQUIRE(test.keyExists("Something") == 0);
		REQUIRE(test.serialize() == "\r\n");
		REQUIRE(test.get("Something") == "");
		REQUIRE(test.getLength() == 0);
		REQUIRE(test.isEmpty() == true);
		test.clear();
		test.erase("something");
		test.mergeFrom(empty);
		test.set("", "");
	}
	SECTION("Simple Headers")
	{
		// Simple tests
		Headers filled;
		Headers empty;
		filled.set("Key", "Value");
		// Checking value of invalid keys
		REQUIRE(empty.get("nonsense") == "");
		REQUIRE(filled.get("nonsense") == "");
		// Checking existance of invalid keys
		REQUIRE(empty.keyExists("nonsense") == false);
		REQUIRE(filled.keyExists("nonsense") == false);
		// Checking value of valid keys
		REQUIRE(empty.get("Key") == "");
		REQUIRE(filled.get("Key") == "Value");
		// Checking existance of valid keys
		REQUIRE(empty.keyExists("Key") == false);
		REQUIRE(filled.keyExists("Key") == true);
		// Check that keys get overwritten instead of added
		filled.set("Key", "WRONG");
		REQUIRE(filled.getLength() == 1);
		// Check if Header is empty
		REQUIRE(empty.isEmpty() == true);
		REQUIRE(filled.isEmpty() == false);
		// Test removing things from Header
		filled.erase("Key");
		REQUIRE(filled.isEmpty() == true);
		REQUIRE(filled.getLength() == 0);
		// Test removing invalid key
		empty.erase("Key");
		REQUIRE(empty.isEmpty() == true);
		// "Test" merging Headers
		empty.set("Type", "https");
		filled.mergeFrom(empty);
		REQUIRE(filled.getLength() == 1);
		// Test clearing Header;
		empty.clear();
		REQUIRE(empty.isEmpty());
		REQUIRE(empty.getLength() == 0);
		// Check Serialization
		REQUIRE(filled.serialize() == "Type: https\r\n\r\n");
	}
	SECTION("Case Sensitivity")
	{
		// Check case insensitivity
		Headers test;

		test.set("KeY", "0");
		test.set("key", "1");
		test.set("Ke", "2");
		test.set("kE", "3");
		test.set("KEyy", "4");
		test.set("keYY", "5");
		REQUIRE(test.getLength() == 3);
		REQUIRE(test.serialize() == "Ke: 3\r\nKeY: 1\r\nKEyy: 5\r\n\r\n");
	}
}
TEST_CASE("CookieJar Tests", "cookie")
{
	SECTION("Unset Cookies")
	{
		Headers head;
		CookieJar cok;
		cok.set(head, "");
		cok.erase("");
		cok.clear();
		cok.getBegin();
		cok.getEnd();
		cok.keyExists("");
		cok.getLength();
		cok.isEmpty();
		cok.get("");
	}
	SECTION("Initialising through objects")
	{
		HttpRequest req;
		HttpResponse res;
		REQUIRE(res.cookies == NULL);
		HttpResponse res2(req);
		REQUIRE(res.cookies != NULL);
	}
	SECTION("Simple tests")
	{
		CookieJar cok;
		Headers head;
		cok.set(head, "Key1=value1;   key2=value2;key3=value3; ");	
		REQUIRE(cok.get("key1") == "value1");
		REQUIRE(cok.get("key2") == "value2");
		REQUIRE(cok.get("key3") == "value3");
		REQUIRE(cok.getLength() == 3);
		REQUIRE(cok.isEmpty() == false);
		REQUIRE(cok.keyExists("key1"));
		REQUIRE(cok.getBegin()->first == "key1")
	}
}

// 		cok.set(head
	// SECTION("CookieJar")
	// {
	// 	{
	// 		std::ofstream out;
	// 		// Check everything on unset CookieJar
	// 		CookieJar test;
	// 		Headers test2;

	// 		test.getBegin();
	// 		test.getEnd();
	// 		out << test;
	// 		REQUIRE(test.keyExists("Something") == 0);
	// 		REQUIRE(test.get("Something") == "");
	// 		REQUIRE(test.getLength() == 0);
	// 		REQUIRE(test.isEmpty() == true);
	// 		test.clear();
	// 		test.erase("something");
	// 		test.set("", "");
	// 		test.setCookieHeaders(test2, "", "", "", 0, false, false, "");
	// 	}
	// 	{
	// 		// Simple tests
	// 		CookieJar filled;
	// 		CookieJar empty;
	// 		filled.set("Key", "Value");
	// 		// Checking value of invalid keys
	// 		REQUIRE(empty.get("nonsense") == "");
	// 		REQUIRE(filled.get("nonsense") == "");
	// 		// Checking existance of invalid keys
	// 		REQUIRE(empty.keyExists("nonsense") == false);
	// 		REQUIRE(filled.keyExists("nonsense") == false);
	// 		// Checking value of valid keys
	// 		REQUIRE(empty.get("Key") == "");
	// 		REQUIRE(filled.get("Key") == "Value");
	// 		// Checking existance of valid keys
	// 		REQUIRE(empty.keyExists("Key") == false);
	// 		REQUIRE(filled.keyExists("Key") == true);
	// 		// Check that keys get overwritten instead of added
	// 		filled.set("Key", "WRONG");
	// 		REQUIRE(filled.getLength() == 1);
	// 		// Check if Header is empty
	// 		REQUIRE(empty.isEmpty() == true);
	// 		REQUIRE(filled.isEmpty() == false);
	// 		// Test removing things from Header
	// 		filled.erase("Key");
	// 		REQUIRE(filled.isEmpty() == true);
	// 		REQUIRE(filled.getLength() == 0);
	// 		// Test removing invalid key
	// 		empty.erase("Key");
	// 		REQUIRE(empty.isEmpty() == true);
	// 		// Test clearing Header;
	// 		empty.clear();
	// 		REQUIRE(empty.isEmpty());
	// 		REQUIRE(empty.getLength() == 0);
	// 	}
	// 	{
	// 		// Check case insensitivity
	// 		CookieJar test;
	// 		test.set("KeY", "0");
	// 		test.set("key", "1");
	// 		test.set("Ke", "2");
	// 		test.set("kE", "3");
	// 		test.set("KEyy", "4");
	// 		test.set("keYY", "5");
	// 		REQUIRE(test.getLength() == 3);
	// 		REQUIRE(test.get("Ke") == "3");
	// 		REQUIRE(test.get("KeY") == "1");
	// 		REQUIRE(test.get("KEyy") == "5");
	// 	}
	// 	{
	// 		// Check merging from Header
	// 		Headers source;
	// 		CookieJar target;

	// 		source.set("ID", "1");
	// 		source.set("IP", "192");
	// 		source.set("Access", "valid");
	// 		source.set("Data", "filled");
	// 		target.set("Access", "invalid");
	// 		target.set("Data", "empty");
	// 		REQUIRE(target.getLength() == 2);
	// 		REQUIRE(target.get("Access") == "invalid");
	// 		REQUIRE(target.get("Data") == "empty");
	// 		target.parseFrom(source);
	// 		REQUIRE(target.getLength() == 4);
	// 		REQUIRE(target.get("ID") == "1");
	// 		REQUIRE(target.get("IP") == "192");
	// 		REQUIRE(target.get("Access") == "valid");
	// 		REQUIRE(target.get("Data") == "filled");
	// 	}
	// }
}