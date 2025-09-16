/* --- ServerPipeline.h --- */

/* ------------------------------------------
Author: undefined
Date: 8/10/2025
------------------------------------------ */

// include/ServerPipeline.h
#ifndef SERVERPIPELINE_H
#define SERVERPIPELINE_H

#include "HttpRequest.h"
#include "HttpResponse.h"
#include "RoutePlan.h"
#include "RouteResolver.h"
#include "Router.h"
#include "ServerConfig.h"
#include "ClientConnection.h"

// Forward declare to avoid heavy include / circular deps
class CGIStreamer;

class ClientConnection;  // forward declare

class ServerPipeline
{
public:
	static bool processRequest(const ServerConfig &cfg,
							int vs_indx,
							HttpRequest &req,
							HttpResponse &res,
							RouteDecision &decision,
							CGIStreamer *cgi,
							ClientConnection *self);   // fix: ClientConnection* not int*
};


#endif