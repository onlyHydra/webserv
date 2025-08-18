#ifndef VIRTUAL_SERVER_H
#define VIRTUAL_SERVER_H

#include <string>
#include <vector>

struct VirtualServer
{
	std::string listen_host;
	int listen_port;
	std::vector<std::string> server_names;
};

#endif
