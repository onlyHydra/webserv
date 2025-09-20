/* --- ServerPipeline.h --- */
/* --- ServerPipeline.h --- */

/* ------------------------------------------
Author: undefined
Date: 8/10/2025
------------------------------------------ */

// include/ServerPipeline.h
#ifndef SERVER_PIPELINE_H
#define SERVER_PIPELINE_H

#include "HttpRequest.h"
#include "HttpResponse.h"
#include "RoutePlan.h"
#include "RouteResolver.h"
#include "Router.h"
#include "ServerConfig.h"

// Forward declare to avoid heavy include / circular deps
class CGIStreamer;

class ServerPipeline
{
public:
    static bool processRequest(const ServerConfig &cfg,
                               int vs_indx,
                               HttpRequest &req,
                               HttpResponse &res,
                               RouteDecision &decision,
                               CGIStreamer* cgi_streamer); // <-- 6th param
};

#endif // SERVER_PIPELINE_H