#define CATCH_CONFIG_MAIN
#include <catch2/catch_all.hpp>
#include "ServerConfig.h"
#include <fstream>
#include <stdexcept>

TEST_CASE("ServerConfig parses minimal config", "[ServerConfig]")
{
	ServerConfig cfg;
	REQUIRE_NOTHROW(cfg.parseFile("tests/unit/config/minimal.conf"));
	REQUIRE(cfg.servers().size() == 1);
	const VirtualServer &vs = cfg.servers()[0];
	REQUIRE(vs.listen_port == 8080);
	REQUIRE(vs.root == "/var/www/html");
	REQUIRE(vs.index_files.size() == 1);
	REQUIRE(vs.index_files[0] == "index.html");
}

TEST_CASE("ServerConfig detects missing brace", "[ServerConfig][invalid]")
{
	ServerConfig cfg;
	REQUIRE_THROWS_AS(cfg.parseFile("tests/unit/config/invalid_missing_brace.conf"), std::runtime_error);
}

TEST_CASE("ServerConfig detects bad port", "[ServerConfig][invalid]")
{
	ServerConfig cfg;
	REQUIRE_THROWS_AS(cfg.parseFile("tests/unit/config/invalid_bad_port.conf"), std::runtime_error);
}

TEST_CASE("ServerConfig parses multiple servers", "[ServerConfig]")
{
	ServerConfig cfg;
	REQUIRE_NOTHROW(cfg.parseFile("tests/unit/config/multi_server.conf"));
	REQUIRE(cfg.servers().size() == 2);
	REQUIRE(cfg.servers()[0].listen_port == 8080);
	REQUIRE(cfg.servers()[1].listen_port == 8081);
}

TEST_CASE("ServerConfig throws on missing listen directive", "[ServerConfig][invalid]") {
    ServerConfig cfg;
    REQUIRE_THROWS_AS(cfg.parseFile("tests/unit/config/invalid_missing_listen.conf"), std::runtime_error);
}


TEST_CASE("ServerConfig throws on unsupported HTTP method", "[ServerConfig][invalid]") {
    ServerConfig cfg;
    REQUIRE_THROWS_AS(cfg.parseFile("tests/unit/config/invalid_method.conf"), std::runtime_error);
}

TEST_CASE("ServerConfig throws on invalid autoindex value", "[ServerConfig][invalid]") {
    ServerConfig cfg;
    REQUIRE_THROWS_AS(cfg.parseFile("tests/unit/config/invalid_autoindex.conf"), std::runtime_error);
}

TEST_CASE("ServerConfig throws on invalid error code", "[ServerConfig][invalid]") {
    ServerConfig cfg;
    REQUIRE_THROWS_AS(cfg.parseFile("tests/unit/config/invalid_error_page.conf"), std::runtime_error);
}

TEST_CASE("ServerConfig throws if config file does not exist", "[ServerConfig][invalid]") {
	ServerConfig cfg;
	REQUIRE_THROWS_AS(cfg.parseFile("tests/unit/config/does_not_exist.conf"), std::runtime_error);
}

TEST_CASE("ServerConfig throws on unknown directive", "[ServerConfig][invalid]") {
	std::ofstream f("tests/unit/config/invalid_unknown_directive.conf");
	f << "server {\nlisten 8080;\nfoo bar;\n}" << std::endl;
	f.close();
	ServerConfig cfg;
	REQUIRE_THROWS_AS(cfg.parseFile("tests/unit/config/invalid_unknown_directive.conf"), std::runtime_error);
}

TEST_CASE("ServerConfig parses valid error_page config", "[ServerConfig]") {
	ServerConfig cfg;
	REQUIRE_NOTHROW(cfg.parseFile("tests/unit/config/valid_error_page.conf"));
	REQUIRE(cfg.servers().size() == 1);
	const VirtualServer &vs = cfg.servers()[0];
	REQUIRE(vs.error_pages.count(404));
	REQUIRE(vs.error_pages.at(404) == "/404.html");
}
