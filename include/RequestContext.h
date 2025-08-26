#if !defined(REQUEST_CONTEXT_H)
#define REQUEST_CONTEXT_H

#include <string>

class ServerConfig;
class VirtualServer;
struct Location;

// forward declare
class ClientConnection;


struct RequestContext
{

	// Who & where
	const ServerConfig *cfg; // server-wide config
	const VirtualServer *vs; // selected virtual server
	const Location *loc;	 // matched location (can be 0)
	int vs_index;			 // index of vs in cfg->servers (optional)
	int local_port;			 // socket's local port

	// RouteDecision extras (filled by Router)
	std::string cgi_ext;	   // e.g. ".php" if CGI chosen
	std::string upstream_name; // name of upstream if proxy

	// Routing helpers (populated by Router/ServerPipeline)
	std::string rel_path;       // path relative to location prefix
	std::string effective_root; // filesystem root to resolve files

	// Optional connection info you may add later:
	// std::string remote_ip;
	// int remote_port;
	// bool is_tls;
	ClientConnection* connection; // add this
	RequestContext()
		: cfg(0), vs(0), loc(0), vs_index(-1), local_port(0) {};
};

#endif // REQUEST_CONTEXT_H
