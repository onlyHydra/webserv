#include <catch2/catch_all.hpp>
#include "Router.h"
#include "RouteResolver.h"
#include "ServerConfig.h"
#include "VirtualServer.h"
#include <string>

TEST_CASE("RouteResolver returns longest prefix and matched_prefix", "[RouteResolver]") {
    ServerConfig cfg;
    VirtualServer vs;
    vs.locations.push_back(Location()); vs.locations.back().path_prefix = "/";
    vs.locations.push_back(Location()); vs.locations.back().path_prefix = "/a";
    vs.locations.push_back(Location()); vs.locations.back().path_prefix = "/a/b";

    std::string matched;
    const Location *L = RouteResolver::matchLocation(vs, "/a/b/c", matched);
    REQUIRE(L != nullptr);
    REQUIRE(matched == "/a/b");
}

TEST_CASE("Router normalizes path and computes rel_path/effective_root", "[Router][normalize]") {
    ServerConfig cfg;
    VirtualServer vs;
    vs.root = "/srv/www";
    Location loc;
    loc.path_prefix = "/a";
    vs.locations.push_back(loc);
    cfg.push_back(vs);

    RouteDecision out;
    Router::makeDecisionForVS(cfg, 0, "GET", "/a//b/./c", out);
    REQUIRE(out.kind == RouteDecision::HK_STATIC);
    REQUIRE(out.rel_path == "/b/c");
    REQUIRE(out.effective_root == "/srv/www");
}

TEST_CASE("Router rejects illegal traversal with '..'", "[Router][normalize]") {
    ServerConfig cfg;
    VirtualServer vs;
    Location loc; loc.path_prefix = "/"; vs.locations.push_back(loc);
    cfg.push_back(vs);

    RouteDecision out;
    Router::makeDecisionForVS(cfg, 0, "GET", "/../etc/passwd", out);
    REQUIRE(out.kind == RouteDecision::HK_ERROR);
    REQUIRE(out.status == 400);
}

TEST_CASE("Router copies try_files and handles HK_RETURN", "[Router][try_files][return]") {
    ServerConfig cfg;
    VirtualServer vs;
    Location loc; loc.path_prefix = "/";
    loc.try_files.push_back("$uri"); loc.try_files.push_back("/index.html");
    loc.return_status = 301; loc.return_target = "/moved";
    vs.locations.push_back(loc);
    cfg.push_back(vs);

    RouteDecision out;
    Router::makeDecisionForVS(cfg, 0, "GET", "/some", out);
    // return_status present should cause HK_RETURN
    REQUIRE(out.kind == RouteDecision::HK_RETURN);
    REQUIRE(out.status == 301);
    REQUIRE(out.return_target == "/moved");
    REQUIRE(out.try_files.size() == 2);
}
