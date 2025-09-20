/* --- ResponseFactory.h --- */


/* ------------------------------------------
Author: undefined
Date: 8/27/2025
------------------------------------------ */

#ifndef RESPONSEFACTORY_H
#define RESPONSEFACTORY_H

#include "HttpResponse.h"

// forward declaration to avoid include cycles
struct RequestContext;

class ResponseFactory {
public:
    static HttpResponse makeError(int code,
                                  const std::string &reasons = "",
                                  bool close = true,
                                  const std::string &bodyText = "");

    static HttpResponse makeText(int code,
                                 const std::string &text,
                                 const std::string &reasons = "",
                                 bool close = true);

    // Unified error helper: serves configured error_page if available,
    // otherwise falls back to a plain text body. Safe path resolution.
    static HttpResponse makeErrorOrPage(const RequestContext &ctx,
                                        int code,
                                        const std::string &reason = "",
                                        bool close = true,
                                        const std::string &fallbackBody = "");
};

#endif // RESPONSEFACTORY_H

