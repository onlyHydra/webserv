/* --- Router.h --- */

/* ------------------------------------------
Author: undefined
Date: 8/10/2025
------------------------------------------ */

#ifndef ROUTER_H
#define ROUTER_H

#include <string>

class ServerConfig;
class RouteDecision;
class Router
{
public:
	static void makeDecisionForVS(const ServerConfig &cfg,
										  int vs_idx,
										  const std::string &method,
										  const std::string &uri,
										  RouteDecision &out);

private:
	Router();
	Router(const Router &);
	Router &operator=(const Router &);
};

#endif // ROUTER_H
