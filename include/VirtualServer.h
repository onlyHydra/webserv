#ifndef VIRTUALSERVER_H
#define VIRTUALSERVER_H

#include <string>
#include <vector>
class VirtualServer
{
public:
	int listen_port;
	std::string listen_host;
	std::vector<std::string> server_names;
	VirtualServer();
	~VirtualServer();

private:
};

#endif // VIRTUALSERVER_H