/* --- CgiHandler.h --- */

/* ------------------------------------------
Author: undefined
Date: 8/10/2025
------------------------------------------ */

#ifndef CGIHANDLER_H
#define CGIHANDLER_H

#include "Handler.h"
#include <vector>
#include <string>

class CgiHandler : virtual public Handler
{
public:
    CgiHandler();
    ~CgiHandler();
    bool handle(HttpRequest &req, HttpResponse &res, RequestContext &ctx);

private:
    // Build "KEY=VALUE" strings for execve() env
	// Returns the number of "KEY=VALUE" entries added to envv (>=0).
	// Always fills sensible defaults; does not indicate success/failure.
    int buildEnv(const HttpRequest&,
             const VirtualServer&,
             std::vector<std::string>&) const;

};

#endif // CGIHANDLER_H



