#include "Router.h"
#include "RouteResolver.h"
#include "ServerConfig.h"
#include "VirtualServer.h"
#include "Location.h"
#include "CgiSpec.h"
#include <map>
#include <string>

static std::string path_from_uri(const std::string &uri)
{
	std::string::size_type q = uri.find('?');
	return (q == std::string::npos) ? uri : uri.substr(0, q);
}

static std::string ext_of(const std::string &path)
{
	std::string::size_type slash = path.rfind('/');
	std::string::size_type dot = path.rfind('.');
	if (dot == std::string::npos)
		return std::string();
	if (slash != std::string::npos && dot < slash)
		return std::string();
	return path.substr(dot);
}

void Router::makeDecisionForVS(const ServerConfig &cfg,
							   int vs_idx,
							   const std::string &method,
							   const std::string &uri,
							   RouteDecision &out)
{
	// Validate VS index
	if (vs_idx < 0 || vs_idx >= (int)cfg.servers.size())
	{
		out.kind = RouteDecision::HK_ERROR;
		out.status = 400;
		out.vs = 0;
		out.loc = 0;
		return;
	}

	const VirtualServer &vs = cfg.servers[vs_idx];
	out.vs = &vs;
	const std::string path = path_from_uri(uri);

	// 1) Choose Location (longest-prefix, no regex yet)
	const Location *L = RouteResolver::matchLocation(vs, path);
	out.loc = L;

	// 2) Method PUT/PATCH gated by write_conf on the matched location
	if ((method == "PUT" || method == "PATCH") && L)
	{
		if ((method == "PUT" && L->write_conf.allow_put) ||
			(method == "PATCH" && L->write_conf.allow_patch))
		{
			out.kind = RouteDecision::HK_PUTPATCH;
			out.status = 200;
			return;
		}
		else
		{
			out.kind = RouteDecision::HK_ERROR;
			out.status = 405;
			return;
		}
	}

	// 3) Proxy?
	if (L && L->is_proxy)
	{
		out.kind = RouteDecision::HK_PROXY;
		out.upstream_name = L->proxy_name;
		out.status = 200;
		return;
	}

	// 4) CGI? Look into location first, then (optionally) server-wide defaults
	const std::string ext = ext_of(path);
	if (!ext.empty())
	{
		std::map<std::string, CgiSpec>::const_iterator it;
		if (L)
		{
			it = L->cgi_by_ext.find(ext);
			if (it != L->cgi_by_ext.end())
			{
				out.kind = RouteDecision::HK_CGI;
				out.cgi_ext = ext;
				out.status = 200;
				return;
			}
		}
		// If you have cfg.cgi_defaults in your real config, check it next:
		// it = cfg.cgi_defaults.find(ext);
		// if (it != cfg.cgi_defaults.end()) { out.kind = RouteDecision::HK_CGI; out.cgi_ext = ext; out.status=200; return; }
	}

	// 5) Static (default)
	out.kind = RouteDecision::HK_STATIC;
	out.status = 200;
}