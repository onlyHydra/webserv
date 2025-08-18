#include <catch2/catch_all.hpp>
#include "Router.h"
#include "RouteResolver.h"
#include "ServerConfig.h"
#include "VirtualServer.h"
#include <string>

TEST_CASE("Router returns error for invalid VS index", "[Router]") {
    ServerConfig cfg;
    VirtualServer vs;
    cfg.push_back(vs);
    RouteDecision out;
    Router::makeDecisionForVS(cfg, -1, "GET", "/", out);
    REQUIRE(out.kind == RouteDecision::HK_ERROR);
    REQUIRE(out.status == 400);
    Router::makeDecisionForVS(cfg, 99, "GET", "/", out);
    REQUIRE(out.kind == RouteDecision::HK_ERROR);
    REQUIRE(out.status == 400);
}

TEST_CASE("Router chooses static handler by default", "[Router]") {
    ServerConfig cfg;
    VirtualServer vs;
    Location loc;
    loc.path_prefix = "/";
    vs.locations.push_back(loc);
    cfg.push_back(vs);
    RouteDecision out;
    Router::makeDecisionForVS(cfg, 0, "GET", "/index.html", out);
    REQUIRE(out.kind == RouteDecision::HK_STATIC);
    REQUIRE(out.status == 200);
}

TEST_CASE("Router chooses CGI handler if extension matches", "[Router]") {
    ServerConfig cfg;
    VirtualServer vs;
    vs.locations.push_back(Location());
    vs.locations.back().path_prefix = "/cgi";
    vs.locations.back().cgi_by_ext[".py"] = CgiSpec{"/usr/bin/python", 1000};
    cfg.push_back(vs);
    RouteDecision out;
    Router::makeDecisionForVS(cfg, 0, "GET", "/cgi/script.py", out);
    REQUIRE(out.kind == RouteDecision::HK_CGI);
    REQUIRE(out.cgi_ext == ".py");
    REQUIRE(out.status == 200);
}

TEST_CASE("Router chooses proxy handler if location is proxy", "[Router]") {
    ServerConfig cfg;
    VirtualServer vs;
    Location loc;
    loc.path_prefix = "/api";
    loc.is_proxy = true;
    loc.proxy_name = "backend";
    vs.locations.push_back(loc);
    cfg.push_back(vs);
    RouteDecision out;
    Router::makeDecisionForVS(cfg, 0, "GET", "/api/resource", out);
    REQUIRE(out.kind == RouteDecision::HK_PROXY);
    REQUIRE(out.upstream_name == "backend");
    REQUIRE(out.status == 200);
}

TEST_CASE("Router chooses PUT/PATCH handler if allowed", "[Router]") {
    ServerConfig cfg;
    VirtualServer vs;
    Location loc;
    loc.path_prefix = "/upload";
    loc.write_conf.allow_put = true;
    loc.write_conf.allow_patch = true;
    vs.locations.push_back(loc);
    cfg.push_back(vs);
    RouteDecision out;
    Router::makeDecisionForVS(cfg, 0, "PUT", "/upload/file", out);
    REQUIRE(out.kind == RouteDecision::HK_PUTPATCH);
    REQUIRE(out.status == 200);
    Router::makeDecisionForVS(cfg, 0, "PATCH", "/upload/file", out);
    REQUIRE(out.kind == RouteDecision::HK_PUTPATCH);
    REQUIRE(out.status == 200);
}

TEST_CASE("Router returns 405 for disallowed PUT/PATCH", "[Router]") {
    ServerConfig cfg;
    VirtualServer vs;
    Location loc;
    loc.path_prefix = "/upload";
    loc.write_conf.allow_put = false;
    loc.write_conf.allow_patch = false;
    vs.locations.push_back(loc);
    cfg.push_back(vs);
    RouteDecision out;
    Router::makeDecisionForVS(cfg, 0, "PUT", "/upload/file", out);
    REQUIRE(out.kind == RouteDecision::HK_ERROR);
    REQUIRE(out.status == 405);
    Router::makeDecisionForVS(cfg, 0, "PATCH", "/upload/file", out);
    REQUIRE(out.kind == RouteDecision::HK_ERROR);
    REQUIRE(out.status == 405);
}
