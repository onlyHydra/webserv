/* --- ServerPipeline.cpp --- */

/* ------------------------------------------
author: undefined
date: 8/10/2025
------------------------------------------ */

#include "ServerPipeline.h"
#include "ServerConfig.h"
#include "HttpRequest.h"
#include "HttpResponse.h"
#include "Router.h"
#include "RouteResolver.h"
#include "StaticHandler.h"
#include "ProxyHandler.h"
#include "CgiHandler.h"
#include "PutPatchHandler.h"
#include "RequestContext.h"
#include "ResponseFactory.h"
#include "UploadHandler.h"
#include "RateLimiter.h"
#include <sys/stat.h>
#include <sstream>
#include <cstring>
#include <cstdio>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>





// Grab client IP from the ClientConnection*, fallback to getsockname/getpeername
static std::string getPeerIPFromFD(int fd) {
    sockaddr_storage ss;
    std::memset(&ss, 0, sizeof ss);
    socklen_t sl = (socklen_t)sizeof ss;
    if (getpeername(fd, (sockaddr*)&ss, &sl) == 0) {
        char buf[INET6_ADDRSTRLEN]; std::memset(buf, 0, sizeof buf);
        if (ss.ss_family == AF_INET) {
            inet_ntop(AF_INET, &((sockaddr_in*)&ss)->sin_addr, buf, sizeof buf);
        } else if (ss.ss_family == AF_INET6) {
            inet_ntop(AF_INET6, &((sockaddr_in6*)&ss)->sin6_addr, buf, sizeof buf);
        } else {
            std::snprintf(buf, sizeof buf, "unknown");
        }
        return std::string(buf);
    }
    return std::string("unknown");
}

static RateLimiter& globalLimiter() {
    static RateLimiter rl;
    return rl;
}

bool ServerPipeline::processRequest(const ServerConfig &cfg,
                                    int vs_indx,
                                    HttpRequest &req,
                                    HttpResponse &res,
                                    RouteDecision &decision,
                                    CGIStreamer *cgi_streamer,
                                    ClientConnection *client)
{
    // ---- Build RequestContext from inputs ----
    RequestContext ctx;
    ctx.cfg = &cfg;
    ctx.vs_index = vs_indx;
    ctx.vs = 0;

    if (ctx.vs_index >= 0 && ctx.vs_index < (int)cfg.servers().size())
        ctx.vs = &cfg.servers()[ctx.vs_index];
    if (!ctx.vs)
    {
        res = ResponseFactory::makeText(500, "No virtual server resolved\n",
                                        "Internal Server Error", /*close*/ true);
        return true;
    }

    // Copy routing decision details
    ctx.loc            = decision.loc;
    ctx.effective_root = decision.effective_root;
    ctx.rel_path       = decision.rel_path;
    ctx.cgi_ext        = decision.cgi_ext;
    ctx.matched_prefix = decision.matched_prefix;
    ctx.upstream_name  = decision.upstream_name;

    // Body/temp defaults
    ctx.temp_file_used = false;
    ctx.temp_filename  = std::string();

    // Proxy timeout defaults (overridden by per-location values if available)
    ctx.proxy_connect_timeout_ms = 5000;
    ctx.proxy_io_idle_timeout_ms = 15000;

    if (ctx.loc)
    {
        if (ctx.loc->proxy_connect_timeout_ms > 0)
            ctx.proxy_connect_timeout_ms = ctx.loc->proxy_connect_timeout_ms;
        if (ctx.loc->proxy_io_idle_timeout_ms > 0)
            ctx.proxy_io_idle_timeout_ms = ctx.loc->proxy_io_idle_timeout_ms;
    }

    // Hook CGI and the owning client connection
    ctx.cgi_streamer = cgi_streamer;
    ctx.client       = client;   // <-- REQUIRED for ProxyHandler to call beginProxyTunnel()

    // ---- Debug: log the chosen route (optional) ----
    // std::fprintf(stderr, "[ROUTE] kind=%d rel=%s upstream=%s client=%p\n",
    //              (int)decision.kind, ctx.rel_path.c_str(), ctx.upstream_name.c_str(), (void*)ctx.client);



    // ---- Rate limiting (before picking handler / heavy work) ----
if (ctx.loc && ctx.loc->rate_limit.enabled) {
    RateBucketCfg cfg;
    // convert requests/minute -> requests/second
    cfg.rps = (ctx.loc->rate_limit.requests_per_minute > 0)
                ? (static_cast<double>(ctx.loc->rate_limit.requests_per_minute) / 60.0)
                : 0.0;
    cfg.burst = (ctx.loc->rate_limit.burst > 0)
                ? static_cast<double>(ctx.loc->rate_limit.burst)
                : 0.0;
    cfg.max_entries = 1024; // simple default store cap

    RateLimiter &rl = globalLimiter();
    rl.setConfig(cfg);

    const int cfd = (ctx.client) ? ctx.client->getFD() : -1;
    const std::string ip = getPeerIPFromFD(cfd);

    unsigned long long now_ms =
    static_cast<unsigned long long>(std::time(NULL)) * 1000ULL;


    RateDecision d = rl.allow(ip, now_ms);
    rl.maybeCleanup(now_ms);

    if (!d.allowed) {
    HttpResponse tooMany = ResponseFactory::makeText(
        429, "Too Many Requests\n", "Too Many Requests", true);

    char ra[32]; std::snprintf(ra, sizeof ra, "%d", d.retry_after_seconds);
    tooMany.headers.set("Retry-After", std::string(ra));

    // Expose rate limit metadata
    char lim[32]; std::snprintf(lim, sizeof lim, "%d", d.limit_rpm);
    tooMany.headers.set("X-RateLimit-Limit", std::string(lim));
    tooMany.headers.set("X-RateLimit-Remaining", "0");

    res = tooMany;
    return true; // short-circuit, no handler
}

}




    // ---- Pick the handler once ----
    Handler *h = 0;
    switch (decision.kind)
    {
    case RouteDecision::HK_STATIC:   h = new StaticHandler();    break;
    case RouteDecision::HK_CGI:      h = new CgiHandler();       break;
    case RouteDecision::HK_PROXY:    h = new ProxyHandler();     break;
    case RouteDecision::HK_PUTPATCH: h = new PutPatchHandler();  break;
    case RouteDecision::HK_UPLOAD:   h = new UploadHandler();    break;

    case RouteDecision::HK_ERROR:
    {
        const int code = decision.status ? decision.status : 500;
        res = ResponseFactory::makeText(code, "", /*reason*/ "", /*close*/ true);
        return true;
    }
    default:
        res = ResponseFactory::makeText(500, "Unhandled route kind\n",
                                        "Internal Server Error", /*close*/ true);
        return true;
    }

    // ---- Execute handler ----
    const bool done = h->handle(req, res, ctx);
    delete h;
    return done;
}
