/* --- ServerConfig.h --- */

/* ------------------------------------------
Author: undefined
Date: 8/10/2025
------------------------------------------ */

#ifndef SERVERCONFIG_H
#define SERVERCONFIG_H

#include "VirtualServer.h"
#include <vector>
class ServerConfig
{
public:
	std::vector<VirtualServer> servers;
	ServerConfig();
	~ServerConfig();

private:
};

#endif // SERVERCONFIG_H
