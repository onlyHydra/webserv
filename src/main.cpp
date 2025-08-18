#include "Server.h"
#include "ServerConfig.h"
#include <csignal>
#include <cstdlib>
#include <iostream>
using namespace std;

static Server *g_srv = 0;
static void on_sig(int)
{
	if (g_srv)
		g_srv->stop();
}

/** For now I get port from ARGV */
int main(int argc, char **argv)
{
	int port = (argc > 1) ? std::atoi(argv[1]) : 8080;

	ServerConfig cfg;
	VirtualServer vs;
	vs.listen_host = "127.0.0.1";
	vs.listen_port = port;
	cfg.servers.push_back(vs);

	Server server(cfg);
	// server.setPipeline(&pipeline);

	g_srv = &server;
	std::signal(SIGINT, on_sig);
	std::signal(SIGTERM, on_sig);

	try
	{
		server.start();
		server.run(50);
	}
	catch (const std::exception &e)
	{
		std::cerr << "fatal: " << e.what() << "\n";
		return 1;
	}
	return 0;
}
