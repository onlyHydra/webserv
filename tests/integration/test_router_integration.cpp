#include <catch2/catch_all.hpp>
#include "Router.h"
#include "ServerConfig.h"
#include "RouteResolver.h"
#include <string>

TEST_CASE("Router integration: complex config, static/upload/error routing", "[Router][integration]") {
    ServerConfig cfg;
    cfg.parseFile("tests/integration/config/router_integration.conf");
    REQUIRE(cfg.servers().size() == 3);
    RouteDecision out;

    // --- Server 0: 0.0.0.0:8080 ---
    // Root static
    Router::makeDecisionForVS(cfg, 0, "GET", "/index.html", out);
    REQUIRE(out.kind == RouteDecision::HK_STATIC);
    // /static location
    Router::makeDecisionForVS(cfg, 0, "GET", "/static/static.html", out);
    REQUIRE(out.kind == RouteDecision::HK_STATIC);
    // /upload location (POST allowed)
    Router::makeDecisionForVS(cfg, 0, "POST", "/upload/file", out);
    REQUIRE(out.kind == RouteDecision::HK_STATIC);
    // /upload location (GET not allowed)
    Router::makeDecisionForVS(cfg, 0, "GET", "/upload/file", out);
    REQUIRE(out.kind == RouteDecision::HK_ERROR);
    // / location (autoindex on)
    Router::makeDecisionForVS(cfg, 0, "GET", "/", out);
    REQUIRE(out.kind == RouteDecision::HK_STATIC);

    // --- Server 1: 127.0.0.1:8081 ---
    // Root static
    Router::makeDecisionForVS(cfg, 1, "GET", "/index.php", out);
    REQUIRE(out.kind == RouteDecision::HK_STATIC);
    // /admin location (POST allowed)
    Router::makeDecisionForVS(cfg, 1, "POST", "/admin/admin.html", out);
    REQUIRE(out.kind == RouteDecision::HK_STATIC);
    // /admin location (GET allowed)
    Router::makeDecisionForVS(cfg, 1, "GET", "/admin/admin.html", out);
    REQUIRE(out.kind == RouteDecision::HK_STATIC);
    // /admin location (DELETE not allowed)
    Router::makeDecisionForVS(cfg, 1, "DELETE", "/admin/admin.html", out);
    REQUIRE(out.kind == RouteDecision::HK_ERROR);

    // --- Server 2: 192.168.1.10:8082 ---
    // Root static
    Router::makeDecisionForVS(cfg, 2, "GET", "/intranet.html", out);
    REQUIRE(out.kind == RouteDecision::HK_STATIC);
    // /api location (POST allowed)
    Router::makeDecisionForVS(cfg, 2, "POST", "/api/api.html", out);
    REQUIRE(out.kind == RouteDecision::HK_STATIC);
    // /api location (DELETE allowed)
    Router::makeDecisionForVS(cfg, 2, "DELETE", "/api/api.html", out);
    REQUIRE(out.kind == RouteDecision::HK_STATIC);
    // /api location (PATCH not allowed)
    Router::makeDecisionForVS(cfg, 2, "PATCH", "/api/api.html", out);
    REQUIRE(out.kind == RouteDecision::HK_ERROR);

    // Error for invalid VS
    Router::makeDecisionForVS(cfg, 99, "GET", "/", out);
    REQUIRE(out.kind == RouteDecision::HK_ERROR);
}
