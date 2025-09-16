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
#include <sys/stat.h>
#include <sstream>
#include <cstdio>
#include <algorithm>
#include <vector>
#include <string>

// Build an RFC-compliant Allow header from a location's allowed_methods,
// making sure HEAD is present if GET is allowed.

/* 

    static std::string build_allow_header(const Location* loc)

Builds a correct Allow header from a location’s configured allowed_methods. 
It first copies the list, then ensures HEAD is included whenever GET is allowed (RFC practice). 
Next it sorts and deduplicates the methods, 
and finally joins them with commas into a single header value (e.g., "GET, HEAD, POST"). 
Centralizing this logic avoids subtle bugs where different handlers might emit inconsistent 
Allow sets, and guarantees 405 responses always advertise exactly which methods are permitted on that route. 
The result is used when routing yields a HK_ERROR with status 405 Method Not Allowed.

*/


static std::string build_allow_header(const Location* loc)
{
    if (!loc)
        return std::string();

    std::vector<std::string> allow = loc->allowed_methods;
    bool hasGet = false, hasHead = false;

    for (size_t i = 0; i < allow.size(); ++i) {
        if (allow[i] == "GET") 
            hasGet  = true;
        if (allow[i] == "HEAD")
            hasHead = true;
    }
    if (hasGet && !hasHead)
        allow.push_back("HEAD");

    std::sort(allow.begin(), allow.end());
    allow.erase(std::unique(allow.begin(), allow.end()), allow.end());

    std::string line;
    for (size_t i = 0; i < allow.size(); ++i) {
        if (i) line += ", ";
        line += allow[i];
    }
    return line;
}


/* 

bool ServerPipeline::processRequest(const ServerConfig& cfg, int vs_indx, HttpRequest& req, HttpResponse& res, RouteDecision& decision, CGIStreamer* cgi_streamer, ClientConnection* self)

Core orchestrator for one request. It assembles a RequestContext from the resolved virtual server, decision (effective root, relative path, 
matched prefix, CGI extension, upstream target), and runtime objects (client connection, CGI streamer). If no VS exists, it returns a 500 text response. 
Otherwise, it selects exactly one handler from the decision kind: Static, CGI, Proxy, Put/Patch, or Upload. 
For HK_ERROR, it either builds a 405 (including Allow from build_allow_header) or uses ResponseFactory::makeErrorOrPage for configured error pages/fallback text. 
It then invokes handle() once, deletes the handler, and returns whether the response is complete (synchronous) or will continue streaming asynchronously.


*/

bool ServerPipeline::processRequest(const ServerConfig &cfg,
                                    int vs_indx,
                                    HttpRequest &req,
                                    HttpResponse &res,
                                    RouteDecision &decision,
                                    CGIStreamer* cgi_streamer,
                                    ClientConnection* self)
{

    // ---- Build RequestContext from inputs ----
    RequestContext ctx;
    ctx.client = self;
    ctx.cfg        = &cfg;
    ctx.vs_index   = vs_indx;
    ctx.vs         = 0;

    if (ctx.vs_index >= 0 && ctx.vs_index < (int)cfg.servers().size())
        ctx.vs = &cfg.servers()[ctx.vs_index];

    // If no VS resolved yet, we cannot map an error_page safely; use plain text.
    if (!ctx.vs) {
        res = ResponseFactory::makeText(500,
                                        "No virtual server resolved\n",
                                        "Internal Server Error",
                                        true /* close */);
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

    // CGI streamer for async CGI
    ctx.cgi_streamer = cgi_streamer;

    // ---- Debug: log the chosen route ----
    std::fprintf(stderr, "[ROUTE] kind=%d path=%s\n",
                 (int)decision.kind, decision.rel_path.c_str());

    // ---- Pick the handler once ----
    Handler *h = 0;
    switch (decision.kind)
    {
        case RouteDecision::HK_STATIC:    h = new StaticHandler();   break;
        case RouteDecision::HK_CGI:       h = new CgiHandler();      break;
        case RouteDecision::HK_PROXY:     h = new ProxyHandler();    break;
        case RouteDecision::HK_PUTPATCH:  h = new PutPatchHandler(); break;
        case RouteDecision::HK_UPLOAD:    h = new UploadHandler();   break;

        case RouteDecision::HK_ERROR:
        {
            const int code = decision.status ? decision.status : 500;

            // 405 requires an Allow header
            if (code == 405) {
                res = ResponseFactory::makeErrorOrPage(ctx, 405, "Method Not Allowed", true);
                const std::string allowLine = build_allow_header(ctx.loc);
                if (!allowLine.empty())
                    res.headers.set("Allow", allowLine);
                return true;
            }

            // All other errors: honor configured error_page (safe) or fallback to plain text
            res = ResponseFactory::makeErrorOrPage(ctx, code, "", true /* close */);
            return true;
        }

        default:
            // Defensive: unexpected route kind → 500
            res = ResponseFactory::makeErrorOrPage(ctx, 500, "Internal Server Error", true /* close */);
            return true;
    }

    // ---- Execute handler ----
    const bool done = h->handle(req, res, ctx);
    delete h;

    // done == true  → synchronous handler produced a full response
    // done == false → asynchronous (e.g., CGI started). Caller should keep connection in PH_WRITE and stream.
    return done;
}


