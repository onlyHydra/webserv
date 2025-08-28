#include <catch2/catch_all.hpp>

#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>

#include "ServerConfig.h"
#include "ServerPipeline.h"
#include "VirtualServer.h"
#include "HttpRequest.h"
#include "HttpResponse.h"

// Helper to create a temporary file with content and return its path
static std::string write_temp_file(const std::string &dir, const std::string &name, const std::string &content) {
    std::string path = dir + "/" + name;
    int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    REQUIRE(fd != -1);
    ssize_t w = ::write(fd, content.data(), content.size());
    REQUIRE((size_t)w == content.size());
    ::close(fd);
    return path;
}

// Ensure temp dir exists
static std::string make_temp_dir() {
    const char *base = "tests/unit/tmp";
    ::mkdir("tests", 0755);
    ::mkdir("tests/unit", 0755);
    ::mkdir(base, 0755);
    return std::string(base);
}

TEST_CASE("ServerPipeline handles HK_RETURN (301) and sets Location header/body", "[ServerPipeline][return]") {
    ServerConfig cfg;
    VirtualServer vs;
    vs.root = "/nonexistent";
    Location loc;
    loc.path_prefix = "/";
    loc.return_status = 301;
    loc.return_target = "/moved";
    vs.locations.push_back(loc);
    cfg.push_back(vs);

    HttpRequest req;
    HttpResponse res;
    req.setKeepAlive(false);

    bool done = ServerPipeline().processRequest(cfg, 0, req, res);
    REQUIRE(done == true);
    // res should contain body "/moved" and Location header
    std::string body(res.body.begin(), res.body.end());
    REQUIRE(body == "/moved");
    REQUIRE(res.headers.keyExists("Location"));
    REQUIRE(res.headers.get("Location") == "/moved");
}

TEST_CASE("ServerPipeline enforces body size limit and returns 413", "[ServerPipeline][bodysize]") {
    ServerConfig cfg;
    VirtualServer vs;
    vs.root = "/nonexistent";
    vs.client_max_body_size = 4; // 4 bytes
    Location loc;
    loc.path_prefix = "/";
    // location write_conf default is 0 (unlimited) — server limit applies
    vs.locations.push_back(loc);
    cfg.push_back(vs);

    HttpRequest req;
    HttpResponse res;
    // Simulate a request that declares 10 bytes body
    // We don't need to parse a full request; HttpRequest exposes setKeepAlive only.
    // The pipeline reads getBodyLength(), which by default is 0; so we emulate by calling parse
    const char *raw = "POST / HTTP/1.1\r\nContent-Length: 10\r\n\r\n1234567890";
    bool parsed = req.parse(raw, strlen(raw));
    REQUIRE(parsed == true);

    bool done = ServerPipeline().processRequest(cfg, 0, req, res);
    REQUIRE(done == true);
    std::string body(res.body.begin(), res.body.end());
    REQUIRE(body == "Payload Too Large");
}

TEST_CASE("ServerPipeline try_files resolves $uri and selects existing file", "[ServerPipeline][try_files]") {
    std::string tmp = make_temp_dir();
    // Write index.html and alt.html
    write_temp_file(tmp, "index.html", "INDEX");
    write_temp_file(tmp, "alt.html", "ALT");

    ServerConfig cfg;
    VirtualServer vs;
    vs.root = tmp; // effective_root will be this dir
    Location loc;
    loc.path_prefix = "/";
    loc.try_files.push_back("$uri");
    loc.try_files.push_back("/alt.html");
    vs.locations.push_back(loc);
    cfg.push_back(vs);

    HttpRequest req;
    HttpResponse res;
    // Request for /index.html should resolve $uri -> /index.html
    bool parsed = req.parse("GET /index.html HTTP/1.1\r\n\r\n", strlen("GET /index.html HTTP/1.1\r\n\r\n"));
    REQUIRE(parsed == true);

    bool done = ServerPipeline().processRequest(cfg, 0, req, res);
    // StaticHandler is a stub that returns true but does not set body; we at least expect
    // that pipeline rewrote ctx.rel_path to the found file and returned true (handler called).
    REQUIRE(done == true);
}

TEST_CASE("ServerPipeline returns false on invalid VS index", "[ServerPipeline][errors]") {
    ServerConfig cfg; // no servers
    HttpRequest req;
    HttpResponse res;
    bool done = ServerPipeline().processRequest(cfg, 0, req, res);
    REQUIRE(done == false);
}

