#if !defined(REQUEST_GUARDS_DEFAULT_MAX_BODY)
#define REQUEST_GUARDS_DEFAULT_MAX_BODY 2000
#endif

#include "RequestGuards.h"
#include "ServerConfig.h"
#include "VirtualServer.h"
#include "Router.h"
#include "RouteResolver.h"
#include <cstdlib>
#include <cctype>

static std::string path_from_uri(const std::string &uri) {
	std::string::size_type q = uri.find('?');
	return (q == std::string::npos) ? uri : uri.substr(0, q);
}

static std::string upper_copy(const std::string &in) {
	std::string out(in);
	for (std::string::size_type i = 0; i < out.size(); ++i)
		out[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[i])));
	return out;
}

static bool header_has_chunked(const Headers &hdrs) {
	const std::string &te = hdrs.get("Transfer-Encoding");
	if (te.empty()) return false;
	std::string s = te;
	for (std::size_t i = 0; i < s.size(); ++i)
		s[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(s[i])));
	return s.find("chunked") != std::string::npos;
}

static bool parse_content_length(const Headers &hdrs, std::size_t &out) {
	const std::string &cl = hdrs.get("Content-Length");
	if (cl.empty()) return false;
	char *endp = 0;
	unsigned long v = std::strtoul(cl.c_str(), &endp, 10);
	if (!endp || *endp != '\0') return false;
	out = static_cast<std::size_t>(v);
	return true;
}

// VS-level max body (0 = unlimited)
static std::size_t lookup_max_body_vs(const ServerConfig &cfg, int vs_idx) {
	const std::vector<VirtualServer> &v = cfg.servers();
	if (vs_idx < 0 || vs_idx >= static_cast<int>(v.size()))
		return REQUEST_GUARDS_DEFAULT_MAX_BODY; // conservative if invalid vs
	int m = v[vs_idx].client_max_body_size; // bytes; 0 = unlimited
	return (m <= 0) ? 0 : static_cast<std::size_t>(m);
}

Preflight RequestGuards::preflight(const ServerConfig &cfg,
								int vs_idx,
								const std::string &method_in,
								const std::string &uri_in,
								const Headers &hdrs,
								RouteDecision *out_route)
{
	Preflight pr;

	RouteDecision dec;
	const std::string path = path_from_uri(uri_in);
	Router::makeDecisionForVS(cfg, vs_idx, method_in, path, dec);
	if (out_route) *out_route = dec;

	// For special Locations, that want special behaviour
	if (dec.kind == RouteDecision::HK_RETURN && dec.status != 0)
		return (pr);

	if (dec.kind == RouteDecision::HK_ERROR && dec.status != 0) {
		pr.ok = false; pr.reject_status = dec.status; pr.reject_reason = "Routing error";
		return pr;
	}

	const std::string method = upper_copy(method_in);
	pr.needs_body = (method == "POST") || (method == "PUT") || (method == "PATCH");

	// cap from VS (0 = unlimited)
	pr.max_body_bytes = lookup_max_body_vs(cfg, vs_idx);

	const bool chunked = header_has_chunked(hdrs);
	std::size_t cl = 0;
	const bool hasCL = parse_content_length(hdrs, cl);

	if (pr.needs_body) {
		if (!hasCL && !chunked) {
			pr.ok = false; pr.reject_status = 411; pr.reject_reason = "Length Required";
			return pr;
		}
		if (pr.max_body_bytes && hasCL && cl > pr.max_body_bytes) {
			pr.ok = false; pr.reject_status = 413; pr.reject_reason = "Payload Too Large";
			return pr;
		}
	}

	return pr;
}
