#include <catch2/catch_all.hpp>
#include "IpList.h"
#include "Atoi.h"

TEST_CASE("Basic", "[iplist][conversion]")
{
	IpList l;
	unsigned int res;
	unsigned int mask;
	SECTION("Simple 1")
	{
		REQUIRE(IpList::stringToIp("192.168.1.42/24", res, mask));
		REQUIRE(res == 3232235818);
		REQUIRE(mask == 4294967040);
	}
	SECTION("Simple 2")
	{
		REQUIRE(IpList::stringToIp("10.0.0.5/8", res, mask));
		REQUIRE(res == 167772165);
		REQUIRE(mask == 4278190080);
	}
	SECTION("Simple 3")
	{
		REQUIRE(IpList::stringToIp("172.16.255.255/16", res, mask));
		REQUIRE(res == 2886795263);
		REQUIRE(mask == 4294901760);
	}
}

TEST_CASE("IP List Parser / Converter with invalid values", "[iplist][invalid][conversion]")
{
	IpList l;
	unsigned int res;
	unsigned int mask;
	SECTION("Just IPs")
	{
		// Out of range (>255)
		REQUIRE(!IpList::stringToIp("256.0.0.1", res, mask));
		REQUIRE(!IpList::stringToIp("192.168.1.999", res, mask));
		REQUIRE(!IpList::stringToIp("300.300.300.300", res, mask));
		// "Negative" numbers
		REQUIRE(!IpList::stringToIp("-1.0.0.0", res, mask));
		REQUIRE(!IpList::stringToIp("192.-1.0.1", res, mask));
		// Missing sockets
		REQUIRE(!IpList::stringToIp("192.168.1", res, mask));
		REQUIRE(!IpList::stringToIp("10.0", res, mask));
		REQUIRE(!IpList::stringToIp("1.2.3.4.5", res, mask));
		// Invalid chars
		REQUIRE(!IpList::stringToIp("192.168.a.1", res, mask));
		REQUIRE(!IpList::stringToIp("abc.def.ghi.jkl", res, mask));
		REQUIRE(!IpList::stringToIp("112.34.56.#78", res, mask));
		// Funny dots
		REQUIRE(!IpList::stringToIp(".192.168.0.1", res, mask));
		REQUIRE(!IpList::stringToIp("192.168.0.1.", res, mask));
		REQUIRE(!IpList::stringToIp(".192.168.a.1.", res, mask));
		// too many dots!
		REQUIRE(!IpList::stringToIp("192..168.1.1", res, mask));
		REQUIRE(!IpList::stringToIp("10.0..0.1", res, mask));
	}
	SECTION("CIDR")
	{
		// Out of range (>32)
		REQUIRE(!IpList::stringToIp("127.0.0.1/33", res, mask));
		REQUIRE(!IpList::stringToIp("127.0.0.1/255", res, mask));
		REQUIRE(!IpList::stringToIp("127.0.0.1/100", res, mask));
		REQUIRE(!IpList::stringToIp("127.0.0.1/-1", res, mask));
		// Invalid chars
		REQUIRE(!IpList::stringToIp("127.0.0.1/a", res, mask));
		REQUIRE(!IpList::stringToIp("127.0.0.1/1a", res, mask));

		// Empty, but not missing
		REQUIRE(!IpList::stringToIp("127.0.0.1/", res, mask));
		REQUIRE(!IpList::stringToIp("127.0.0.1/ ", res, mask));
		// Plain wrong
		REQUIRE(!IpList::stringToIp("127.0.0.1//https://google.com", res, mask));
		REQUIRE(!IpList::stringToIp("127.0.0.1/127.0.0.1", res, mask));
	}
	SECTION("Very wrong")
	{
		REQUIRE(!IpList::stringToIp("", res, mask));
		REQUIRE(!IpList::stringToIp("1", res, mask));
		REQUIRE(!IpList::stringToIp("Alphabet", res, mask));
		REQUIRE(!IpList::stringToIp("/24", res, mask));
		REQUIRE(!IpList::stringToIp("/lol", res, mask));
	}
}