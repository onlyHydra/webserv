#include <catch2/catch_all.hpp>
#include <iostream>
#include <fstream>
#include <ostream>
#include "Headers.h"
#include "CookieJar.h"
#include "HttpResponse.h"
#include "HttpRequest.h"
#include <ostream>	
#include <unistd.h>
#include <stdlib.h>

static void pushHeaderMaxEntries(Headers &h)
{
	for (int i = 0; i < HEADER_ENTRY_LIMIT - 1; ++i)
	{
		std::ostringstream oss;
		oss << i;
		std::string str = oss.str();
		h.set(str, "");
	}
	REQUIRE(h.getEntryCount() == HEADER_ENTRY_LIMIT - 1);
	REQUIRE(h.getRealEntryCount() == HEADER_ENTRY_LIMIT - 1);
}

static void pushHeaderMaxBytes(Headers &h)
{
	int length = HEADER_BYTE_LIMIT - 5;
	char character = '*';
	std::string longStr(length, character);
	REQUIRE(h.set("1", longStr));
}

static void invalidSetCookie(Headers &h, CookieJar &cok, const std::string &combo)
{
	size_t oldEntCount = h.getEntryCount();
	size_t oldRealEntCount = h.getRealEntryCount();
	size_t oldByteCount = h.getByteSize();
	size_t oldRealByteCount = h.getRealByteSize();
	REQUIRE(cok.set(h, combo) == HTTP_HEADER_TOO_BIG);
	REQUIRE(h.getEntryCount() == oldEntCount);
	REQUIRE(h.getRealEntryCount() == oldRealEntCount);
	REQUIRE(h.getByteSize() == oldByteCount);
	REQUIRE(h.getRealByteSize() == oldRealByteCount);
}

static void invalidSetHeader(Headers &h, const std::string &key, const std::string &value)
{
	size_t oldEntCount = h.getEntryCount();
	size_t oldRealEntCount = h.getRealEntryCount();
	size_t oldByteCount = h.getByteSize();
	size_t oldRealByteCount = h.getRealByteSize();
	REQUIRE(!h.set(key, value));
	REQUIRE(h.getEntryCount() == oldEntCount);
	REQUIRE(h.getRealEntryCount() == oldRealEntCount);
	REQUIRE(h.getByteSize() == oldByteCount);
	REQUIRE(h.getRealByteSize() == oldRealByteCount);
}

static void check400(Headers &h, CookieJar &cok, const std::string &input)
{
	int oldLength = cok.getLength();
	REQUIRE(cok.set(h, input) == HTTP_BAD_REQUEST);
	REQUIRE(cok.getLength() == oldLength);
	if (oldLength == 0)
	{
		REQUIRE(cok.isEmpty());
		REQUIRE(cok.getBegin() == cok.getEnd());
	}
}

static void check200(Headers &h, CookieJar &cok, const std::string &input)
{
	int oldLength = cok.getLength();
	REQUIRE(cok.set(h, input) == HTTP_OK);
	REQUIRE(cok.getLength() == oldLength + 1);
}

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
	SECTION("Size checks")
	{
		Headers h;
		// Adding existing key
		REQUIRE(h.set("key", "1234"));
		REQUIRE(h.getByteSize() == 7);
		REQUIRE(h.getRealByteSize() == 7);
		REQUIRE(h.getEntryCount() == 1);
		REQUIRE(h.getRealEntryCount() == 1);
		// Adding new key longer then old one
		REQUIRE(h.set("key", "123456789"));
		REQUIRE(h.getByteSize() == 12);
		REQUIRE(h.getRealByteSize() == 12);
		REQUIRE(h.getEntryCount() == 1);
		REQUIRE(h.getRealEntryCount() == 1);
		// Adding new key shorter then old one
		REQUIRE(h.set("key", "1"));
		REQUIRE(h.getByteSize() == 4);
		REQUIRE(h.getRealByteSize() == 4);
		REQUIRE(h.getEntryCount() == 1);
		REQUIRE(h.getRealEntryCount() == 1);
	}
	SECTION("Check Byte Limit")
	{
		// Push Header to just before its Byte Limit
		Headers h;
		pushHeaderMaxBytes(h);
		// Header now has only 4 bytes left
		REQUIRE(h.getEntryCount() == 1);
		REQUIRE(h.getRealEntryCount() == 1);
		REQUIRE(h.getByteSize() == HEADER_BYTE_LIMIT - 4);
		REQUIRE(h.getRealByteSize() == HEADER_BYTE_LIMIT - 4);

		// Try to exceed Byte Limit
		invalidSetHeader(h, "", "abcefghhiklmnop");
		invalidSetHeader(h, "abcefghhiklmnop", "");
		REQUIRE(h.set("new", "1"));
		invalidSetHeader(h, "new", "12345");
		REQUIRE(h.get("new") == "1");
		REQUIRE(h.getByteSize() == HEADER_BYTE_LIMIT);
	}
	SECTION("Check Entry Limit")
	{
		// Push Header to just before its Entry Limit
		Headers h;
		pushHeaderMaxEntries(h);
		// Header now has only 1 entry left
		REQUIRE(h.getEntryCount() == HEADER_ENTRY_LIMIT - 1);
		REQUIRE(h.getRealEntryCount() == HEADER_ENTRY_LIMIT - 1);

		// Set last Entry
		REQUIRE(h.set("last one", ""));
	
		// Try to exceed Entry Limit
		invalidSetHeader(h, "", "abcefghhiklmnop");
		invalidSetHeader(h, "abcefghhiklmnop", "");
		REQUIRE(h.getEntryCount() == HEADER_ENTRY_LIMIT);
		REQUIRE(h.getRealEntryCount() == HEADER_ENTRY_LIMIT);
	}
}

TEST_CASE("CookieJar Tests", "[http][cookie]")
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
		Headers h;
		REQUIRE(res.cookies == NULL);
		HttpResponse res2(req);
		req.cookies.set(h, "key=value");
		REQUIRE(res2.cookies == &req.cookies);
		REQUIRE(res2.cookies != NULL);
		REQUIRE(res2.cookies->get("key") == "value");
	}
	SECTION("Simple tests")
	{
		CookieJar cok;
		Headers head;
		REQUIRE(cok.set(head, "key1=value1;   key2=value2;key3=value3; ") == HTTP_OK);	
		REQUIRE(cok.get("key1") == "value1");
		REQUIRE(cok.get("key2") == "value2");
		REQUIRE(cok.get("key3") == "value3");
		REQUIRE(cok.getLength() == 3);
		REQUIRE(cok.isEmpty() == false);
		REQUIRE(cok.keyExists("key1"));
		// Deleting one
		cok.erase("key2");
		REQUIRE(!cok.keyExists("key2"));
		REQUIRE(cok.get("key2") == "");
		REQUIRE(cok.getLength() == 2);
		// Deleting all
		cok.clear();
		REQUIRE(!cok.keyExists("key1"));
		REQUIRE(cok.get("key2") == "");
		REQUIRE(cok.getLength() == 0);
		REQUIRE(cok.isEmpty());
	}
	SECTION("Iterating")
	{
		{ // Empty list
			CookieJar cok;
			Headers head;
			std::map<std::string, std::string, CiLess>::const_iterator posA;
			std::map<std::string, std::string, CiLess>::const_iterator posB;
			posA = cok.getBegin();
			posB = cok.getEnd();
			REQUIRE(posA == posB);
		}
		{ // Filled list
			CookieJar cok;
			Headers head;
			int i = 0;
			std::map<std::string, std::string, CiLess>::const_iterator posA;
			std::map<std::string, std::string, CiLess>::const_iterator posB;
			cok.set(head, "1=a; 2=b; 3=c");
			posA = cok.getBegin();
			posB = cok.getEnd();
			for (; posA != posB; posA++)
			{
				int key = (posA->first)[0];
				int value = (posA->second)[0];
				REQUIRE(key == (int)(i + '1'));
				REQUIRE(value == (int)(i + 'a'));
				i++;
			}
			REQUIRE(posA == posB);
			REQUIRE(posA == cok.getEnd());
			REQUIRE(i == 3);
		}
	}
	SECTION("Consecutive setting")
	{
		CookieJar cok;
		Headers head;
		REQUIRE(cok.set(head, "key1=value1; ") == HTTP_OK);
		REQUIRE(cok.getLength() == 1);
		REQUIRE(cok.set(head, "key2=value2") == HTTP_OK);
		REQUIRE(cok.getLength() == 2);
		REQUIRE(cok.set(head, "key3=value3; ") == HTTP_OK);
		REQUIRE(cok.getLength() == 3);
		REQUIRE(cok.get("key1") == "value1");
		REQUIRE(cok.get("key2") == "value2");
		REQUIRE(cok.get("key3") == "value3");
	}
	SECTION("Syntax invalid, no changes")
	{
		CookieJar cok;
		Headers head;
		int oldLength;
		{ // "a" -> Invalid Syntax, no changes in Cookies
			oldLength = cok.getLength();
			REQUIRE(cok.set(head, "a") == HTTP_BAD_REQUEST);
			REQUIRE(cok.getLength() == oldLength);
			REQUIRE(cok.isEmpty());
			REQUIRE(cok.getBegin() == cok.getEnd());
		}
		{ // "abc" -> Invalid Syntax, no changes in Cookies
			oldLength = cok.getLength();
			REQUIRE(cok.set(head, "abc") == HTTP_BAD_REQUEST);
			REQUIRE(cok.getLength() == oldLength);
			REQUIRE(cok.isEmpty());
			REQUIRE(cok.getBegin() == cok.getEnd());
		}
	}
	SECTION("Syntax invalid, with changes")
	{
		CookieJar cok;
		Headers head;
		int oldLength;
		{ // "abc=value; abc" -> First key valid, after that not valid
			REQUIRE(cok.set(head, "abc=value; def") == HTTP_BAD_REQUEST);
		}
		{ // "abc=; def; ghi" -> First key valid, after that not valid
			REQUIRE(cok.set(head, "abc=; def; ghi") == HTTP_BAD_REQUEST);
		}
	}
	SECTION("Syntax valid, no changes")
	{
		CookieJar cok;
		Headers head;
		int oldLength;
		{ // Empty string -> no entry
			REQUIRE(cok.set(head, "") == HTTP_OK);
			REQUIRE(cok.getLength() == 0);
			REQUIRE(cok.isEmpty());
		}
	}
	SECTION("Syntax valid, with changes")
	{
		CookieJar cok;
		Headers head;
		int oldLength;
		check200(head, cok, "abc=");
		check200(head, cok, "jkl=;");
		check200(head, cok, "mno=;   ");
		check200(head, cok, "lan=en-US");
		oldLength = cok.getLength();
		REQUIRE(cok.set(head, "def=;        ghi=lel") == HTTP_OK);
		REQUIRE(cok.getLength() == oldLength + 2);
		oldLength = cok.getLength();
		REQUIRE(cok.set(head, "SID=lel, who gives a shit; 456=lol") == HTTP_OK);
		REQUIRE(cok.getLength() == oldLength + 2);
	}
	SECTION("Cookies -> Enviroment")
	{
		HttpRequest req;
		HttpResponse res(req);

		std::string parse = "POST / HTTP/1.1\r\nCookie: key=value\r\nCookie: lan=en-US\r\n";
		req.parse(parse.c_str(), parse.size());
		REQUIRE(res.cookies->prepareForCgi() == "key=value; lan=en-US");
	}
}

TEST_CASE("Mixing CookieJar and Header", "[http][cookie][header]")
{
	SECTION("Testing Cookie and Header connection")
	{
		Headers h;
		CookieJar cok;
		cok.set(h, "lang=en-Us");
		REQUIRE(h.getByteSize() == 9);
		REQUIRE(h.getRealByteSize() == 0);
		REQUIRE(h.getEntryCount() == 1);
		REQUIRE(h.getRealEntryCount() == 0);
	}
	SECTION("Bytesize Limit Testing")
	{
		Headers h;
		CookieJar cok;
		pushHeaderMaxBytes(h);
		// Header now has only 4 bytes left
		REQUIRE(h.getEntryCount() == 1);
		REQUIRE(h.getRealEntryCount() == 1);
		REQUIRE(h.getByteSize() == HEADER_BYTE_LIMIT - 4);
		REQUIRE(h.getRealByteSize() == HEADER_BYTE_LIMIT - 4);

		// Try to exceed Byte Limit
		invalidSetCookie(h, cok, "ab=cefghhiklmnop");
		REQUIRE(cok.set(h, "a=bcd") == HTTP_OK);
		invalidSetCookie(h, cok, "b=");
		REQUIRE(cok.get("a") == "bcd");
		REQUIRE(h.getByteSize() == HEADER_BYTE_LIMIT);
		REQUIRE(cok.set(h, "a=") == HTTP_OK);
		REQUIRE(h.getByteSize() == HEADER_BYTE_LIMIT - 3);
		REQUIRE(h.getEntryCount() == 2);
		REQUIRE(h.getRealEntryCount() == 1);
	}
	SECTION("Entry Limit Testing")
	{
		Headers h;
		CookieJar cok;
		pushHeaderMaxEntries(h);
		// Header now has only 1 entry left
		REQUIRE(h.getEntryCount() == HEADER_ENTRY_LIMIT - 1);
		REQUIRE(h.getRealEntryCount() == HEADER_ENTRY_LIMIT - 1);

		// Set last Entry
		REQUIRE(cok.set(h, "last=valid"));

		// Try to exceed Entry Limit
		invalidSetCookie(h, cok, "ab=cefghhiklmnop");
		REQUIRE(cok.set(h, "last=also valid") == HTTP_OK);
		invalidSetCookie(h, cok, "b=");
		REQUIRE(cok.get("last") == "also valid");
		REQUIRE(h.getEntryCount() == HEADER_ENTRY_LIMIT);
		REQUIRE(h.getRealEntryCount() == HEADER_ENTRY_LIMIT - 1);
	}
}
