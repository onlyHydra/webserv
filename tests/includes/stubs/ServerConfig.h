#ifndef SERVER_CONFIG_H
#define SERVER_CONFIG_H

#include <vector>
#include "VirtualServer.h"

struct ServerConfig
{
	std::vector<VirtualServer> servers;
};

#endif
