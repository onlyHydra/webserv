#ifndef REQUEST_CONTEXT_H
#define REQUEST_CONTEXT_H

#include <string>
#include "ServerConfig.h"
#include "VirtualServer.h"
#include "ClientConnection.h"
#include "CGIStreamer.h"

// Forward declaration to avoid circular include
class ClientConnection;

struct RequestContext
{
	// --- Who & where ---
	const ServerConfig *cfg;     // server-wide config
	const VirtualServer *vs;     // selected virtual server
	const Location *loc;         // matched location (can be 0)
	int  vs_index;               // index of vs in cfg->servers (optional)
	int  local_port;             // socket's local port

	// --- RouteDecision extras (from Router) ---
	std::string cgi_ext;         // e.g. ".php" if CGI chosen
	std::string upstream_name;   // "host:port" for proxy

	// --- Body storage info ---
	bool        temp_file_used;  // true if the request body was stored in a temp file
	std::string temp_filename;   // path to the temp file

	// --- Routing helpers (from Router/ServerPipeline) ---
	std::string rel_path;        // path relative to location prefix
	std::string effective_root;  // filesystem root to resolve files
	std::string matched_prefix;

	// --- Proxy timeouts (ms) ---
	int proxy_connect_timeout_ms;   // connect deadline
	int proxy_io_idle_timeout_ms;   // idle I/O deadline

	// --- connection-scoped helpers ---
	CGIStreamer      *cgi_streamer;   // non-owning pointer to ClientConnection’s CGIStreamer
	ClientConnection *client;         // back-pointer to current ClientConnection (set by caller if available)

	RequestContext()
		: cfg(0),
		  vs(0),
		  loc(0),
		  vs_index(-1),
		  local_port(0),
		  cgi_ext(),
		  upstream_name(),
		  temp_file_used(false),
		  temp_filename(),
		  rel_path(),
		  effective_root(),
		  matched_prefix(),
		  proxy_connect_timeout_ms(5000),
		  proxy_io_idle_timeout_ms(15000),
		  cgi_streamer(0),
		  client(0)
	{}
};

#endif // REQUEST_CONTEXT_H


