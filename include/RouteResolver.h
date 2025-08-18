/* --- RouteResolver.h --- */

/* ------------------------------------------
Author: undefined
Date: 8/10/2025
------------------------------------------ */

#ifndef ROUTERESOLVER_H
#define ROUTERESOLVER_H

#include "VirtualServer.h"
#include "ServerConfig.h"
#include <string>

struct RouteDecision
{
	enum HandlerKind
	{
		HK_STATIC,
		HK_CGI,
		HK_PROXY,
		HK_PUTPATCH,
		HK_ERROR
	};
	HandlerKind kind;
	int status; // for HK_ERROR
	const VirtualServer *vs;
	const Location *loc;
	std::string cgi_ext;
	std::string upstream_name;

	RouteDecision() : kind(HK_ERROR), status(500), vs(0), loc(0) {}
};

class RouteResolver
{
public:
	static const Location *matchLocation(const VirtualServer &vs, const std::string &path);

private:
	RouteResolver();
	RouteResolver(const RouteResolver &);
	RouteResolver &operator=(const RouteResolver &);
};

#endif // ROUTERESOLVER_H
