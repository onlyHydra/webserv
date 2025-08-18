#include "Router.h"
#include "RouteResolver.h"
#include "ServerConfig.h"
#include "VirtualServer.h"
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

// Helper: Validate VS index
static bool isValidVSIndex(const ServerConfig &cfg, int vs_idx)
{
	return vs_idx >= 0 && vs_idx < (int)cfg.servers().size();
}

// Helper: Find matching location
static const Location *findLocation(const VirtualServer &vs, const std::string &path)
{
	return RouteResolver::matchLocation(vs, path);
}

// Helper: Check allowed methods (except PUT/PATCH)
static bool isMethodAllowed(const Location *L, const std::string &method)
{
	if (!L || L->allowed_methods.empty() || method == "PUT" || method == "PATCH")
		return true;
	for (std::vector<std::string>::const_iterator it = L->allowed_methods.begin(); it != L->allowed_methods.end(); ++it)
	{
		if (*it == method)
			return true;
	}
	return false;
}

// Helper: Check PUT/PATCH allowed
static bool isPutPatchAllowed(const Location *L, const std::string &method)
{
	if (!L)
		return false;
	if (method == "PUT" && L->write_conf.allow_put)
		return true;
	if (method == "PATCH" && L->write_conf.allow_patch)
		return true;
	return false;
}

// Helper: Check proxy
static bool isProxy(const Location *L)
{
	return L && L->is_proxy;
}

// Helper: Check CGI
static bool isCGI(const Location *L, const std::string &ext)
{
	if (!L)
		return false;
	return L->cgi_by_ext.find(ext) != L->cgi_by_ext.end();
}

void Router::makeDecisionForVS(const ServerConfig &cfg,
							   int vs_idx,
							   const std::string &method,
							   const std::string &uri,
							   RouteDecision &out)
{
	if (!isValidVSIndex(cfg, vs_idx))
	{
		out.kind = RouteDecision::HK_ERROR;
		out.status = 400;
		out.vs = 0;
		out.loc = 0;
		return;
	}

	const VirtualServer &vs = cfg.servers()[vs_idx];
	out.vs = &vs;
	const std::string path = path_from_uri(uri);
	const Location *L = findLocation(vs, path);
	out.loc = L;

	// Check allowed methods (except PUT/PATCH)
	if (!isMethodAllowed(L, method))
	{
		out.kind = RouteDecision::HK_ERROR;
		out.status = 405;
		return;
	}

	// PUT/PATCH
	if ((method == "PUT" || method == "PATCH") && !isPutPatchAllowed(L, method))
	{
		out.kind = RouteDecision::HK_ERROR;
		out.status = 405;
		return;
	}
	if ((method == "PUT" || method == "PATCH") && isPutPatchAllowed(L, method))
	{
		out.kind = RouteDecision::HK_PUTPATCH;
		out.status = 200;
		return;
	}

	// Proxy
	if (isProxy(L))
	{
		out.kind = RouteDecision::HK_PROXY;
		out.upstream_name = L->proxy_name;
		out.status = 200;
		return;
	}

	// CGI
	const std::string ext = ext_of(path);
	if (!ext.empty() && isCGI(L, ext))
	{
		out.kind = RouteDecision::HK_CGI;
		out.cgi_ext = ext;
		out.status = 200;
		return;
	}

	// Static (default)
	if (L)
	{
		out.kind = RouteDecision::HK_STATIC;
		out.status = 200;
		return;
	}
	else
	{
		out.kind = RouteDecision::HK_STATIC;
		out.status = 200;
		return;
	}
}