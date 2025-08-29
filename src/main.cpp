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
    const std::string path = (argc > 1) ? argv[1] : "config/extended.conf";

    // Handle signals early
    std::signal(SIGINT,  on_sig);
    std::signal(SIGTERM, on_sig);
#ifdef SIGPIPE
    std::signal(SIGPIPE, SIG_IGN); // avoid crashes on client hangups
#endif

    try {
        ServerConfig cfg;
        cfg.parseFile(path);          // <-- now inside try

        Server server(cfg);
        g_srv = &server;

        server.start();
        server.run(50);               // poll timeout ms
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << "\n";
        return 1;
    }
    catch (...) {
        std::cerr << "fatal: unknown error\n";
        return 1;
    }
}
