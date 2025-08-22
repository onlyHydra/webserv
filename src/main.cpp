#include "Server.h"
#include "ServerConfig.h"
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>

using namespace std;

static Server *g_srv = 0;
static void on_sig(int)
{
	if (g_srv)
		g_srv->stop();
}



/** For now I get port from ARGV */
int main(int argc, char** argv)
{
    const std::string &path = (argc > 1)? argv[1] : "config/extended.conf";

    ServerConfig cfg;            // no argv-taking ctor anymore


	cfg.parseFile(path);
    Server server(cfg);
    // server.setPipeline(&pipeline);

    g_srv = &server;
    std::signal(SIGINT, on_sig);
    std::signal(SIGTERM, on_sig);

    try {
        server.start();
        server.run(50);
    } catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
