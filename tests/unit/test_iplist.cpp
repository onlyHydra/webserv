#include <catch2/catch_all.hpp>
#include "IpList.h"
#include "Atoi.h"
#include "ServerConfig.h"

TEST_CASE("IP List Conversions", "[iplist][conversion]")
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

TEST_CASE("IpList Allowing and Denying", "[iplist][allow][deny]")
{
	SECTION("Allowing 3")
	{
		IpList l;
		unsigned int ip;
		unsigned int mask;
		std::string sample;
		
		sample = "127.0.0.2/24";
		l.addAllowRule(sample);
		l.stringToIp(sample, ip, mask);
		REQUIRE(l.allowed[0].base == IpList::getNetwork(ip, mask));
		REQUIRE(l.allowed[0].mask == mask);

		sample = "128.0.0.1/24";
		l.addAllowRule(sample);
		l.stringToIp(sample, ip, mask);
		REQUIRE(l.allowed[1].base == IpList::getNetwork(ip, mask));
		REQUIRE(l.allowed[1].mask == mask);

		sample = "129.0.0.1/24";
		l.addAllowRule(sample);
		l.stringToIp(sample, ip, mask);
		REQUIRE(l.allowed[2].base == IpList::getNetwork(ip, mask));
		REQUIRE(l.allowed[2].mask == mask);

		REQUIRE(l.allowed.size() == 3);
	}
	SECTION("Denying 3")
	{
		IpList l;
		unsigned int ip;
		unsigned int mask;
		std::string sample;
		
		sample = "127.0.0.2/24";
		l.addDenyRule(sample);
		l.stringToIp(sample, ip, mask);
		REQUIRE(l.denied[0].base == IpList::getNetwork(ip, mask));
		REQUIRE(l.denied[0].mask == mask);

		sample = "128.0.0.1/24";
		l.addDenyRule(sample);
		l.stringToIp(sample, ip, mask);
		REQUIRE(l.denied[1].base == IpList::getNetwork(ip, mask));
		REQUIRE(l.denied[1].mask == mask);

		sample = "129.0.0.1/24";
		l.addDenyRule(sample);
		l.stringToIp(sample, ip, mask);
		REQUIRE(l.denied[2].base == IpList::getNetwork(ip, mask));
		REQUIRE(l.denied[2].mask == mask);

		REQUIRE(l.denied.size() == 3);
	}
	SECTION("Mixing both")
	{
		IpList l;
		unsigned int ip;
		unsigned int mask;
		std::string sample;
		
		sample = "127.0.0.2/24";
		l.addAllowRule(sample);
		l.stringToIp(sample, ip, mask);
		REQUIRE(l.allowed[0].base == IpList::getNetwork(ip, mask));
		REQUIRE(l.allowed[0].mask == mask);

		sample = "128.0.0.1/24";
		l.addDenyRule(sample);
		l.stringToIp(sample, ip, mask);
		REQUIRE(l.denied[0].base == IpList::getNetwork(ip, mask));
		REQUIRE(l.denied[0].mask == mask);

		REQUIRE(l.allowed.size() == 1);
		REQUIRE(l.denied.size() == 1);
	}
}

TEST_CASE("IP Ranges", "[iplist][allow][deny][range]")
{
	SECTION("Simple allowance")
	{
		IpList l;

		l.defAllow = false;
		// This Rule should allow .0 - .31
		l.addAllowRule("127.0.0.0/27");
		// This Rule should allow .64 - .95
		l.addAllowRule("127.0.0.64/27");
		REQUIRE(l.checkIp("127.0.0.0") == true);
		REQUIRE(l.checkIp("127.0.0.1") == true);
		REQUIRE(l.checkIp("127.0.0.2") == true);
		REQUIRE(l.checkIp("127.0.0.31") == true);

		REQUIRE(l.checkIp("127.0.0.32") == false);
		REQUIRE(l.checkIp("127.0.0.63") == false);

		REQUIRE(l.checkIp("127.0.0.64") == true);
		REQUIRE(l.checkIp("127.0.0.95") == true);
	}
	SECTION("Make sure CIDR of given is ignored")
	{
		IpList l;

		l.defAllow = false;
		l.addAllowRule("127.0.0.42/32");
		l.checkIp("127.0.0.42/24");
	}
	SECTION("Exact IPs")
	{
		IpList l;

		l.defAllow = false;
		l.addAllowRule("127.0.0.42");
		l.addAllowRule("127.0.1.42/32");
		l.checkIp("127.0.0.42");
		l.checkIp("127.0.1.42");
		l.checkIp("127.0.0.42/32");
		l.checkIp("127.0.1.42/32");
	}
}

TEST_CASE("IpList with ServerConfig", "[ServerConfig][iplist]")
{
	SECTION("Allowing")
	{
		ServerConfig cfg;
		unsigned int ip;
		unsigned int mask;
		unsigned int netAdr;

		std::string sample = "127.0.0.42/13";
		cfg.parseString(std::string("allowIp { ") + sample + std::string(" }"));
		IpList::stringToIp(sample, ip, mask);
		netAdr = IpList::getNetwork(ip, mask);
		REQUIRE(cfg.ip_list.allowed[0].base == netAdr);
		REQUIRE(cfg.ip_list.allowed[0].mask == mask);
	}
	SECTION("Denying")
	{
		ServerConfig cfg;
		unsigned int ip;
		unsigned int mask;
		unsigned int netAdr;

		std::string sample = "122.0.0.42/13";
		cfg.parseString(std::string("denyIp { ") + sample + std::string(" }"));
		IpList::stringToIp(sample, ip, mask);
		netAdr = IpList::getNetwork(ip, mask);
		REQUIRE(cfg.ip_list.denied[0].base == netAdr);
		REQUIRE(cfg.ip_list.denied[0].mask == mask);
	}
	SECTION("Default IP Behaviour allow")
	{
		ServerConfig cfg;
		unsigned int ip;
		unsigned int mask;
		unsigned int netAdr;

		std::string sample = "defaultAllowIp true ;";
		cfg.parseString(sample);
		REQUIRE(cfg.ip_list.defAllow == true);
	}
	SECTION("Default IP Behaviour deny")
	{
		ServerConfig cfg;
		unsigned int ip;
		unsigned int mask;
		unsigned int netAdr;

		std::string sample = "defaultAllowIp false ;";
		cfg.parseString(sample);
		REQUIRE(cfg.ip_list.defAllow == false);
	}
	SECTION("Default IP Behaviour wildcard")
	{
		ServerConfig cfg;
		unsigned int ip;
		unsigned int mask;
		unsigned int netAdr;

		std::string sample = "defaultAllowIp banananana ;";
		cfg.parseString(sample);
		REQUIRE(cfg.ip_list.defAllow == false);
	}
}

TEST_CASE("IpList simulate full execution", "[ServerConfig][iplist]")
{
	SECTION("1")
	{
		ServerConfig cfg;
		cfg.parseString("allowIp { 127.0.0.0/24 128.0.0.0/24 } denyIp { 100.100.100.100 } defaultAllowIp false;");
		{	// Check if parsing into Config worked
			IpList &l = cfg.ip_list;
			unsigned int ip;
			unsigned int mask;
			unsigned int netAdr;
			IpList::stringToIp("127.0.0.0/24", ip, mask);
			netAdr = IpList::getNetwork(ip, mask);
			REQUIRE(l.allowed[0].base == netAdr);
			REQUIRE(l.allowed[0].mask == mask);
			IpList::stringToIp("128.0.0.0/24", ip, mask);
			netAdr = IpList::getNetwork(ip, mask);
			REQUIRE(l.allowed[1].base == netAdr);
			REQUIRE(l.allowed[1].mask == mask);
			IpList::stringToIp("100.100.100.100", ip, mask);
			netAdr = IpList::getNetwork(ip, mask);
			REQUIRE(l.denied[0].base == netAdr);
			REQUIRE(l.denied[0].mask == mask);
			REQUIRE(l.defAllow == false);
		}

		{ // Connect with valid IP
			struct sockaddr_in si;
			si.sin_addr.s_addr = 16777343; // the format that our ip will be in
			struct sockaddr_storage *ss = (struct sockaddr_storage *)&si;
			std::string ip = IpList::getIpFromSocket(ss);
			REQUIRE(ip == "127.0.0.1");
			REQUIRE(cfg.ip_list.checkIp(ip));
		}
		{ // Connect with denied IP
			struct sockaddr_in si;
			si.sin_addr.s_addr = 126; // the format that our ip will be in
			struct sockaddr_storage *ss = (struct sockaddr_storage *)&si;
			std::string ip = IpList::getIpFromSocket(ss);
			REQUIRE(ip == "126.0.0.0");
			REQUIRE(!cfg.ip_list.checkIp(ip));
		}
	}
}
