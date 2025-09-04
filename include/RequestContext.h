#ifndef REQUEST_CONTEXT_H
#define REQUEST_CONTEXT_H

#include <string>
#include "ServerConfig.h"
#include "VirtualServer.h"
#include "ClientConnection.h"
#include "CGIStreamer.h"

struct RequestContext
{
    // --- Who & where ---
    const ServerConfig *cfg;     // server-wide config
    const VirtualServer *vs;     // selected virtual server
    const Location *loc;         // matched location (can be 0)
    int vs_index;                // index of vs in cfg->servers (optional)
    int local_port;               // socket's local port

    // --- RouteDecision extras (from Router) ---
    std::string cgi_ext;         // e.g. ".php" if CGI chosen
    std::string upstream_name;   // name of upstream if proxy

    // --- Body storage info ---
    bool        temp_file_used;  // true if the request body was stored in a temp file
    std::string temp_filename;   // path to the temp file

    // --- Routing helpers (from Router/ServerPipeline) ---
    std::string rel_path;        // path relative to location prefix
    std::string effective_root;  // filesystem root to resolve files
    std::string matched_prefix;   // <--- ADD THIS

    // --- NEW: connection-scoped CGI streamer ---
    CGIStreamer *cgi_streamer;   // non-owning pointer to the ClientConnection’s CGIStreamer

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
          cgi_streamer(0)  // init to null
    {}
};

#endif // REQUEST_CONTEXT_H

