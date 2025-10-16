#ifndef SERVER_PIPELINE_H
#define SERVER_PIPELINE_H

// Forward declarations (avoid heavy includes / cycles)
class ServerConfig;
class HttpRequest;
class HttpResponse;
class CGIStreamer;
class ClientConnection;
struct RouteDecision;

class ServerPipeline
{
public:
    // New API: pass the owning ClientConnection so ProxyHandler can start the tunnel
    static bool processRequest(const ServerConfig &cfg,
                               int vs_indx,
                               HttpRequest &req,
                               HttpResponse &res,
                               RouteDecision &decision,
                               CGIStreamer *cgi_streamer,
                               ClientConnection *client);

    // Backward-compatible overload (old call sites)
    static bool processRequest(const ServerConfig &cfg,
                               int vs_indx,
                               HttpRequest &req,
                               HttpResponse &res,
                               RouteDecision &decision,
                               CGIStreamer *cgi_streamer)
    {
        return processRequest(cfg, vs_indx, req, res, decision, cgi_streamer, 0);
    }
};

#endif // SERVER_PIPELINE_H
