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
#include <sys/stat.h>
#include <sstream>

bool ServerPipeline::processRequest(const ServerConfig &cfg, int vs_indx, HttpRequest &req, HttpResponse &res, RouteDecision &decision)
{
	RequestContext ctx;
	ctx.cfg = &cfg;
	ctx.vs_index = vs_indx;
	ctx.vs = NULL;
	if (ctx.vs == NULL && ctx.vs_index >= 0 && ctx.vs_index < (int)cfg.servers().size())
		ctx.vs = &cfg.servers()[ctx.vs_index];

	if (ctx.vs == NULL)
	{
		// setError(rep, 500, "No virtual server resolved");
		return false;
	}
	ctx.loc = decision.loc;

	// make handlers location-aware using fields that exist in RequestContext
	ctx.loc = decision.loc;
	ctx.effective_root = decision.effective_root;
	ctx.rel_path = decision.rel_path;

	// <<<

	// Pick the handler
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
	case RouteDecision::HK_ERROR:
		return false;
		break;
	default:
		// setError(res, dec.status, toReason(dec.status));
		return true;
	}

	const bool done = h->handle(req, res, ctx); // true => response complete
	delete h;
	return done;
}
