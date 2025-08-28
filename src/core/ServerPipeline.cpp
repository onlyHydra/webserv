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
#include <sys/stat.h>
#include <sstream>

bool ServerPipeline::processRequest(const ServerConfig &cfg, int vs_indx, HttpRequest &req, HttpResponse &res)
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
	

	RouteDecision decision;
	Router::makeDecisionForVS(cfg, vs_indx, req.getMethod(), req.getPath(), decision);

	if (decision.kind == RouteDecision::HK_ERROR)
		return false;

	ctx.loc = decision.loc;
	ctx.cgi_ext = decision.cgi_ext;
	ctx.upstream_name = decision.upstream_name;
	if (decision.vs)
		ctx.vs = decision.vs; // Router may refine VS

	ctx.rel_path = decision.rel_path;
	ctx.effective_root = decision.effective_root;

	// HK_RETURN: immediate response
	if (decision.kind == RouteDecision::HK_RETURN)
	{
		// If redirect-like status, set Location header; otherwise put target in body
		if (!decision.return_target.empty())
		{
			if (decision.status >= 300 && decision.status < 400)
				ctx.cfg->servers()[vs_indx].locations; // no-op to avoid unused warning
		}
		// build a simple body containing the return_target (or empty)
		std::string body = decision.return_target;
		res.body.assign(body.begin(), body.end());
		res.bodyLength = res.body.size();
		std::ostringstream oss;
		oss << res.bodyLength;
		res.headers.set(std::string("Content-Length"), oss.str());
		if (decision.status >= 300 && decision.status < 400 && !decision.return_target.empty())
			res.headers.set(std::string("Location"), decision.return_target);
		return true;
	}

	// Enforce body size limits (location then server)
	{
		size_t reqBody = req.getBodyLength();
		size_t locLimit = 0;
		if (ctx.loc)
			locLimit = ctx.loc->write_conf.max_body_bytes;
		size_t srvLimit = ctx.vs ? ctx.vs->client_max_body_size : 0;
		size_t limit = (locLimit > 0) ? locLimit : srvLimit;
		if (limit > 0 && reqBody > (size_t)limit)
		{
			// prepare simple 413 body
			std::string body = "Payload Too Large";
			res.body.assign(body.begin(), body.end());
			res.bodyLength = res.body.size();
			std::ostringstream oss;
			oss << res.bodyLength;
			res.headers.set(std::string("Content-Length"), oss.str());
			return true; // response ready (caller will send and close)
		}
	}

	// Evaluate try_files tokens if present
	if (!decision.try_files.empty())
	{
		for (std::vector<std::string>::const_iterator it = decision.try_files.begin(); it != decision.try_files.end(); ++it)
		{
			std::string token = *it;
			std::string candidate;
			if (token == std::string("$uri"))
				candidate = ctx.effective_root + ctx.rel_path;
			else if (!token.empty() && token[0] == '/')
				candidate = ctx.effective_root + token;
			else
				candidate = ctx.effective_root + token; // relative token

			struct stat st;
			if (stat(candidate.c_str(), &st) == 0)
			{
				// Found a matching file — rewrite rel_path to candidate-relative and dispatch static
				if (candidate.find(ctx.effective_root) == 0)
				{
					ctx.rel_path = candidate.substr(ctx.effective_root.size());
					if (ctx.rel_path.empty() || ctx.rel_path[0] != '/')
						ctx.rel_path = std::string("/") + ctx.rel_path;
				}
				decision.kind = RouteDecision::HK_STATIC;
				break;
			}
		}
	}

	// Pick the handler
	Handler *h = 0;
    switch (decision.kind) {
        case RouteDecision::HK_STATIC:		h = new StaticHandler();	break;
        case RouteDecision::HK_CGI:			h = new CgiHandler();		break;
        case RouteDecision::HK_PROXY:		h = new ProxyHandler();		break;
        case RouteDecision::HK_PUTPATCH:	h = new PutPatchHandler();	break;
        case RouteDecision::HK_ERROR:		return false;				break;
        default:
            // setError(res, dec.status, toReason(dec.status));
            return true;
    }

	const bool done = h->handle(req, res, ctx); // true => response complete
	delete h;
	return done;
}