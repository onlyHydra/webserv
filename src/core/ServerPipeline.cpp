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

bool ServerPipeline::processRequest(const ServerConfig &cfg,
									int vs_indx,
									HttpRequest &req,
									HttpResponse &res,
									RouteDecision &decision,
									CGIStreamer *cgi_streamer)
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
	ctx.loc = decision.loc;
	ctx.effective_root = decision.effective_root;
	ctx.rel_path = decision.rel_path;
	ctx.cgi_ext = decision.cgi_ext;
	ctx.matched_prefix = decision.matched_prefix;
	ctx.upstream_name = decision.upstream_name;

	// Body/temp defaults
	ctx.temp_file_used = false;
	ctx.temp_filename = std::string();

	// CGI streamer for async CGI
	ctx.cgi_streamer = cgi_streamer;

	// ---- Debug: log the chosen route (avoid fallthrough warnings) ----
	#if defined(DEBUG)
	fprintf(stderr, "[ROUTE] kind=%d path=%s\n",
			(int)decision.kind, decision.rel_path.c_str());
	#endif

	// ---- Pick the handler once ----
	Handler *h = 0;
	switch (decision.kind)
	{
	case RouteDecision::HK_STATIC:
		h = new StaticHandler();
		break;
	case RouteDecision::HK_CGI:
		h = new CgiHandler();
		break;
	case RouteDecision::HK_PROXY:
		h = new ProxyHandler();
		break;
	case RouteDecision::HK_PUTPATCH:
		h = new PutPatchHandler();
		break;
	case RouteDecision::HK_UPLOAD:
		h = new UploadHandler();
		break;

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
