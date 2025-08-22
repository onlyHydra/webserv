// Catch2 tests for config parsing/validation (nginx‑flavored)
//
// Build (example):
//   g++ -std=c++11 -Wall -Wextra -Iinclude \
//       tests/test_config.cpp \
//       src/config/ServerConfig.cpp \
//       VirtualServer.cpp Location.cpp RouteResolver.cpp \
//       -o build/test_config
//
// If you use Catch2 v3, change the include to <catch2/catch_all.hpp>.

#define CATCH_CONFIG_MAIN
#include <catch2/catch_all.hpp>

#include "ServerConfig.h"
#include "VirtualServer.h"
#include "RouteResolver.h"

using std::string;

static ServerConfig parse_ok(const char* text)
{
    ServerConfig cfg;
    REQUIRE_NOTHROW(cfg.parseString(text));
    return cfg;
}

static void parse_fail(const char* text)
{
    ServerConfig cfg;
    REQUIRE_THROWS(cfg.parseString(text));
}

// ---------- Happy paths ----------
TEST_CASE("single server: listen by port only", "[parse][listen]") {
    const char* conf = R"CONF(
        server {
            listen 8080;
            root /var/www/html;
            index index.html;
        }
    )CONF";

    ServerConfig cfg = parse_ok(conf);
    REQUIRE(cfg.servers().size() == 1);
    const VirtualServer& vs = cfg.servers()[0];
    CHECK(vs.listen_host == "");       // wildcard host
    CHECK(vs.listen_port == 8080);
    CHECK(vs.root == "/var/www/html");
    REQUIRE(vs.index_files.size() == 1);
    CHECK(vs.index_files[0] == "index.html");
}

TEST_CASE("listen host:port (one token)", "[parse][listen]") {
    const char* conf = R"CONF(
        server {
            listen 127.0.0.1:8081;
            root /www;
            index i.htm;
        }
    )CONF";

    ServerConfig cfg = parse_ok(conf);
    const VirtualServer& vs = cfg.servers()[0];
    CHECK(vs.listen_host == "127.0.0.1");
    CHECK(vs.listen_port == 8081);
}

TEST_CASE("listen host + port (two tokens)", "[parse][listen]") {
    const char* conf = R"CONF(
        server {
            listen 127.0.0.1 9090;
            root /srv;
            index index.htm;
        }
    )CONF";

    ServerConfig cfg = parse_ok(conf);
    const VirtualServer& vs = cfg.servers()[0];
    CHECK(vs.listen_host == "127.0.0.1");
    CHECK(vs.listen_port == 9090);
}

TEST_CASE("server_name list and error_page mapping", "[parse][server]") {
    const char* conf = R"CONF(
        server {
            listen 8080;
            server_name example.com www.example.com;
            root /var/www;
            index index.html index.htm;
            error_page 404 /errors/404.html;
            error_page 500 /errors/500.html; # semicolon optional per parser
        }
    )CONF";

    ServerConfig cfg = parse_ok(conf);
    const VirtualServer& vs = cfg.servers()[0];
    REQUIRE(vs.server_names.size() == 2);
    CHECK(vs.server_names[0] == "example.com");
    CHECK(vs.server_names[1] == "www.example.com");

    REQUIRE(vs.index_files.size() == 2);
    CHECK(vs.index_files[0] == "index.html");
    CHECK(vs.index_files[1] == "index.htm");

    REQUIRE(vs.error_pages.size() == 2);
    CHECK(vs.error_pages.find(404) != vs.error_pages.end());
    CHECK(vs.error_pages.find(500) != vs.error_pages.end());
    CHECK(vs.error_pages.find(404)->second == "/errors/404.html");
}

TEST_CASE("location basics: root/index/autoindex/methods", "[parse][location]") {
    const char* conf = R"CONF(
        server {
            listen 8080;
            root /var/www;
            index index.html;
            location /static {
                root /var/www/static;
                index main.html assets.html;
                autoindex on;
                methods GET POST;
            }
        }
    )CONF";

    ServerConfig cfg = parse_ok(conf);
    const VirtualServer& vs = cfg.servers()[0];
    REQUIRE(vs.locations.size() == 1);
    const Location& L = vs.locations[0];
    CHECK(L.path_prefix == "/static");
    CHECK(L.root == "/var/www/static");
    CHECK(L.autoindex == true);
    REQUIRE(L.index_files.size() == 2);
    CHECK(L.index_files[0] == "main.html");
    CHECK(L.index_files[1] == "assets.html");
    REQUIRE(L.allowed_methods.size() == 2);
    CHECK(L.allowed_methods[0] == "GET");
    CHECK(L.allowed_methods[1] == "POST");
}

TEST_CASE("comments and stray semicolons are tolerated", "[tokenizer]") {
    const char* conf = R"CONF(
        # a comment line
        ; ; ;
        server { # comment after token
            listen 8080; # trailing
            root /r; index i; ;
            location / { autoindex off; }
        }
    )CONF";

    ServerConfig cfg = parse_ok(conf);
    REQUIRE(cfg.servers().size() == 1);
    const VirtualServer& vs = cfg.servers()[0];
    CHECK(vs.listen_port == 8080);
    CHECK(vs.root == "/r");
    REQUIRE(vs.locations.size() == 1);
    CHECK(vs.locations[0].autoindex == false);
}

// ---------- Failure paths / validation ----------
TEST_CASE("missing semicolon after root", "[errors]") {
    const char* conf = R"CONF(
        server {
            listen 8000;
            root /var/www   # <-- missing ';'
        }
    )CONF";
    parse_fail(conf);
}

TEST_CASE("unterminated server block", "[errors]") {
    const char* conf = R"CONF(
        server {
            listen 8000;
            root /x;
    )CONF"; // no closing '}'
    parse_fail(conf);
}

TEST_CASE("invalid listen port: negative or out of range", "[errors][listen]") {
    const char* conf1 = R"CONF(
        server { listen -1; root /r; index i; }
    )CONF";
    const char* conf2 = R"CONF(
        server { listen 99999; root /r; index i; }
    )CONF";
    parse_fail(conf1);
    parse_fail(conf2);
}

TEST_CASE("invalid listen token host:badport", "[errors][listen]") {
    const char* conf = R"CONF(
        server { listen 127.0.0.1:abc; root /r; index i; }
    )CONF";
    parse_fail(conf);
}

TEST_CASE("server missing listen", "[errors][listen]") {
    const char* conf = R"CONF(
        server { root /r; index i; }
    )CONF";
    parse_fail(conf);
}

TEST_CASE("duplicate listen across servers rejected", "[errors][duplicate]") {
    const char* conf = R"CONF(
        server { listen 127.0.0.1:8080; root /a; index i; }
        server { listen 127.0.0.1 8080; root /b; index j; }
    )CONF";
    parse_fail(conf);
}

TEST_CASE("error_page with non-HTTP status rejected", "[errors][error_page]") {
    const char* conf1 = R"CONF(
        server { listen 1; error_page 200 /x; root /r; index i; }
    )CONF";
    const char* conf2 = R"CONF(
        server { listen 1; error_page abc /x; root /r; index i; }
    )CONF";
    parse_fail(conf1);
    parse_fail(conf2);
}

TEST_CASE("methods: only GET/POST/DELETE allowed", "[errors][methods]") {
    const char* conf = R"CONF(
        server {
            listen 8080;
            root /r; index i;
            location / { methods GET PUT PATCH FOO; }
        }
    )CONF";
    parse_fail(conf);
}

TEST_CASE("unknown directive at top level", "[errors][syntax]") {
    const char* conf = R"CONF(
        garbage on;
        server { listen 1; root /r; index i; }
    )CONF";
    parse_fail(conf);
}

TEST_CASE("unknown directive inside server", "[errors][syntax]") {
    const char* conf = R"CONF(
        server {
            listen 1;
            potato fries;   # unknown
            root /r; index i;
        }
    )CONF";
    parse_fail(conf);
}

TEST_CASE("unknown directive inside location", "[errors][syntax]") {
    const char* conf = R"CONF(
        server {
            listen 1;
            root /r; index i;
            location /x { waffle on; }
        }
    )CONF";
    parse_fail(conf);
}

// ---------- (optional) nginx-like precedence check for locations ----------
TEST_CASE("location precedence: longest prefix wins", "[location][resolver]") {
    const char* conf = R"CONF(
        server {
            listen 9090;
            root /root; index i;
            location / { autoindex off; }
            location /app { autoindex on; }
            location /app/admin { autoindex off; }
        }
    )CONF";

    ServerConfig cfg = parse_ok(conf);
    const VirtualServer& vs = cfg.servers()[0];
    const Location* L1 = RouteResolver::matchLocation(vs, "/app/foo");
    REQUIRE(L1 != 0);
    CHECK(L1->path_prefix == "/app");
    CHECK(L1->autoindex == true);

    const Location* L2 = RouteResolver::matchLocation(vs, "/app/admin/panel");
    REQUIRE(L2 != 0);
    CHECK(L2->path_prefix == "/app/admin");
    CHECK(L2->autoindex == false);

    const Location* L3 = RouteResolver::matchLocation(vs, "/unmatched");
    // falls back to "/" location per our simple rule
    REQUIRE(L3 != 0);
    CHECK(L3->path_prefix == "/");
}
