/* --- Router.h --- */

/* ------------------------------------------
Author: undefined
Date: 8/10/2025
------------------------------------------ */

#ifndef ROUTER_H
#define ROUTER_H

#include <string>

class ServerConfig;
struct RouteDecision;

	/**
	 * Compute routing decision for a resolved virtual server index.
	 *
	 * This function inspects the provided ServerConfig and the request
	 * properties (HTTP method and request-target / URI) and fills `out`
	 * with a `RouteDecision` that tells the core which handler kind to
	 * invoke (STATIC, CGI, PROXY, PUT/PATCH, RETURN or ERROR) and also
	 * surfaces helper fields the pipeline/handlers need (matched prefix,
	 * rel_path, effective_root, try_files, cgi_ext, upstream_name,
	 * rate_limit, allow/deny lists, return_target, ...).
	 *
	 * Parameters:
	 *  - cfg: full server configuration (read-only).
	 *  - vs_idx: index of the virtual server to use (must be a valid index
	 *            into cfg.servers()). If invalid, `out` will be set to
	 *            HK_ERROR with an appropriate status (400).
	 *  - method: HTTP request method (e.g. "GET", "POST").
	 *  - uri: request target (the function will strip the query string
	 *         portion and normalize the path; no percent-decoding is
	 *         performed).
	 *  - out: output RouteDecision populated by the function.
	 *
	 * Behavior and warnings:
	 *  - makeDecisionForVS only decides which handler should be used and
	 *    copies relevant location-level policy into `out`. It does NOT
	 *    enforce policies such as rate-limiting, allow/deny checks,
	 *    try_files resolution, or body-size limits; those responsibilities
	 *    belong to the ServerPipeline/handlers.
	 *  - The function performs basic path normalization (collapsing
	 *    duplicate slashes, removing '.' segments, and rejecting literal
	 *    ".." traversals). It intentionally does not perform
	 *    percent-decoding — if you need to treat encoded sequences as
	 *    semantics-bearing, decode before calling this API.
	 *  - On success, `out.kind` is set to the chosen HandlerKind and
	 *    `out.status` is set to 200 (or a different status for HK_RETURN
	 *    or HK_ERROR). Handlers should consult the other fields on `out`.
	 */
class Router
{
public:

	static void makeDecisionForVS(const ServerConfig &cfg,
						  int vs_idx,
						  const std::string &method,
						  const std::string &uri,
						  RouteDecision &out);


private:
	Router();
	Router(const Router &);
	Router &operator=(const Router &);
};

#endif // ROUTER_H
