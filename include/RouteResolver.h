/* --- RouteResolver.h --- */

/* ------------------------------------------
Author: undefined
Date: 8/10/2025
------------------------------------------ */

#ifndef ROUTERESOLVER_H
#define ROUTERESOLVER_H

#include "VirtualServer.h"
#include "ServerConfig.h"
#include <string>
#include <vector>

/**
 * RouteDecision
 *
 * Represents the routing decision produced by Router/RouteResolver.
 *
 * Fields:
 *  - kind: which handler kind the pipeline should use (STATIC/CGI/PROXY/PUTPATCH/RETURN/ERROR).
 *  - status: when kind==HK_ERROR or HK_RETURN, contains the HTTP status to emit.
 *  - vs, loc: pointers into the configuration describing the chosen virtual server
 *    and location (may be NULL for defaults).
 *  - cgi_ext, upstream_name, return_target: additional hints for handlers.
 *  - try_files, rate_limit, allow_list, deny_list: location-level policy copied
 *    into the decision so the pipeline can enforce them.
 *  - matched_prefix, rel_path, effective_root: helper routing fields computed by Router
 *    to simplify handler path resolution.
 *
 * Note: RouteDecision is an output structure only; callers should not modify the
 * configuration objects referenced by `vs`/`loc`.
 */
struct RouteDecision
{
	enum HandlerKind
	{
		HK_STATIC,
		HK_CGI,
		HK_PROXY,
		HK_RETURN,
		HK_PUTPATCH,
		HK_UPLOAD, 
		HK_ERROR
	};

	HandlerKind kind;
	int status; // for HK_ERROR
	const VirtualServer *vs;
	const Location *loc;
	std::string cgi_ext;
	std::string upstream_name;

	// Policy and helper fields surfaced from Location
	std::vector<std::string> try_files;
	RateLimitConfig rate_limit;
	std::vector<std::string> allow_list;
	std::vector<std::string> deny_list;
	// for HK_RETURN: target (url or text)
	std::string return_target;
	// Routing helpers
	std::string matched_prefix;
	std::string rel_path;
	std::string effective_root;

	RouteDecision() : kind(HK_ERROR), status(500), vs(0), loc(0), cgi_ext(), upstream_name(), try_files(), rate_limit(), allow_list(), deny_list(), return_target(), matched_prefix(), rel_path(), effective_root() {}
	void reset()
	{
		kind = HK_ERROR;
		status = 500;
		vs = 0;
		loc = 0;
		cgi_ext.erase();
		upstream_name.erase();
		try_files.clear();
		allow_list.clear();
		deny_list.clear();
		return_target.erase();
		matched_prefix.erase();
		rel_path.erase();
		effective_root.erase();
	}
	~RouteDecision(){
		vs =0;
		loc = 0;
	};
};

class RouteResolver
{
public:
	/**
	 * Find the best matching Location for the given request path.
	 *
	 * Semantics:
	 *  - Exact match wins immediately.
	 *  - Otherwise the longest literal prefix match wins.
	 *  - Regex locations are ignored by this overload.
	 *
	 * Parameters:
	 *  - vs: the VirtualServer to search locations on.
	 *  - path: the request path (should be normalized; Router performs basic
	 *          normalization before calling this function).
	 *
	 * Returns:
	 *  - pointer to the matched Location or NULL if none matched.
	 */
	static const Location *matchLocation(const VirtualServer &vs, const std::string &path);

	/**
	 * Variant of matchLocation that also returns the matched prefix string.
	 *
	 * The matched_prefix output is set to the Location::path_prefix of the
	 * chosen location (or cleared if no location matched). This is useful
	 * to compute the rel_path handed to handlers.
	 */
	static const Location *matchLocation(const VirtualServer &vs, const std::string &path, std::string &matched_prefix);

private:
	RouteResolver();
	RouteResolver(const RouteResolver &);
	RouteResolver &operator=(const RouteResolver &);
};

#endif // ROUTERESOLVER_H
