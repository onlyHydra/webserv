#include "ServerConfig.h"
#include "HTTPCODES.h"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <cctype>
#include <cstdlib>
#include <set>
#include <climits> // INT_MAX
#include <limits.h>   // PATH_MAX
#include <stdlib.h>   // realpath
#include "Util.h"

/*

static std::string makeAbsolute(const std::string& path)

This helper normalizes any filesystem path found in the config (e.g., root,
upload_store, client_body_temp_path) to an absolute, canonical form.
It calls Util::realpath into a fixed buffer (PATH_MAX) and returns the resolved
string on success; if resolution fails, it gracefully returns the original input.
Normalizing early prevents surprises later when handlers open files: static serving,
upload destinations, autoindex, and CGI script resolution all benefit from predictable
absolute paths. Because it’s a tiny, self-contained utility, it stays static in this
translation unit and is used at every site that ingests a path token during parsing
(e.g., in root, client_body_temp_path, upload_dir, upload_store).
This keeps the parser lean and centralizes path safety in one place,
reducing duplication and ensuring platform-consistent behavior.


*/

static std::string makeAbsolute(const std::string &path)
{
	char buf[PATH_MAX];
	if (Util::realpath(path.c_str(), buf))
		return std::string(buf);
	return path; // fallback if resolution fails
}

/*

static bool parseUnsigned(const std::string& s, int& out)

A minimal, allocation-free parser for small non-negative
integers used across directives like ports, weights, timeouts,
and rate limits. It rejects empty strings and any non-digit character,
accumulates the value in a long, and applies a conservative guard (e.g., > 1,000,000)
to avoid accidental overflow from misconfigurations. On success,
it stores the value in out and returns true. The parser is intentionally
strict (no whitespace, no signs, no locale) because the configuration
language is simple and NGINX-like. It’s used both in top-level directives (e.g., cgi .ext timeout)
and nested blocks (upstream weight=, rate-limit options) to keep error
reporting deterministic and early. By keeping this helper local and small,
the file avoids pulling in bigger parsing machinery while still giving precise failures.

*/

// ---------- tiny helpers (only those we use) ----------
static bool parseUnsigned(const std::string &s, int &out)
{
	if (s.empty())
		return false;
	long v = 0;
	for (std::string::size_type i = 0; i < s.size(); ++i)
	{
		if (s[i] < '0' || s[i] > '9')
			return false;
		v = v * 10 + (s[i] - '0');
		if (v > 1000000L)
			return false; // guard
	}
	out = static_cast<int>(v);
	return true;
}

/*

static void validateErrorStatusOrThrow(const std::string& raw)

This validator ensures that error_page directives reference only valid HTTP
error codes in the client/server error range (400–599). It converts the token with
parseUnsigned, then checks bounds against HTTP_BAD_REQUEST and HTTP_SERVER_ERROR,
throwing with a clear message if the value is invalid. Centralizing the check
makes the main parser simpler and guarantees consistent diagnostics for all error_page entries.
The result prevents impossible mappings like error_page 200 /ok.html; that would later
lead to confusing runtime behavior. When the value is valid, the calling site
records the mapping into the VirtualServer’s error_pages map, which downstream
handlers (e.g., response factory/static handler) consult when synthesizing error
responses. Early failure here saves time by stopping on broken config rather than
letting the server start in a partially invalid state.

*/

static void validateErrorStatusOrThrow(const std::string &raw)
{
	int code = -1;
	if (!parseUnsigned(raw, code) || code < HTTP_BAD_REQUEST || code > HTTP_SERVER_ERROR)
	{
		std::ostringstream oss;
		oss << "invalid error_page status code: '" << raw << "'";
		throw std::runtime_error(oss.str());
	}
}

// ---------- ServerConfig impl ----------

/*


ServerConfig::ServerConfig() / ~ServerConfig()

The constructor initializes the global configuration object into a safe,
empty state: no servers, sessions disabled, empty cookie attributes,
zero timeouts, empty MIME map and CGI defaults, and a fresh IP list.
This guarantees that parsing routines can freely push_back servers and write
into maps without checking for prior initialization. The destructor is trivial
because the class holds only standard containers and value types; RAII handles cleanup.
The object is the authoritative source for process-wide settings: MIME types, CGI defaults,
global IP allow/deny, upstream pools, and the vector of VirtualServer definitions. After parsing,
Server consumes this structure to create listeners and to build request-time resolvers
(router, MIME lookup, CGI registry). Keeping construction simple and deterministic
makes parseFile/parseString idempotent and easy to test.


*/

ServerConfig::ServerConfig()
	: _servers(), session_enabled(false), session_cookie_name(), session_max_age(0), session_secure(false), session_http_only(false), session_same_site(), mime_mapping(), cgi_defaults(), ip_list()
{
}

ServerConfig::~ServerConfig() {}

/*

void ServerConfig::push_back(const VirtualServer& vs)

A tiny convenience that appends a fully parsed server { … }
block to the internal _servers vector. It’s called from within parseTokens
once a server block has been completely consumed and validated (e.g., it had a listen directive).
This isolates the mutation point for the server list and keeps the parser’s control flow clear:
build VirtualServer vs; → fill fields while reading tokens → push_back(vs) when done. Downstream,
Server iterates _servers to create sockets, register AcceptorHandlers in the EventLoop,
and wire per-server routing (locations, error pages, limits). Having a stable
append API also makes it simpler to add post-parse validation (like duplicate listen checks)
after all servers are collected.


*/

void ServerConfig::push_back(const VirtualServer &vs)
{
	_servers.push_back(vs);
}

/*

bool ServerConfig::canOpen(const char* path) const

A quick capability probe used by tooling or preflight checks.
It tries to open the provided path with std::ifstream and returns whether
the stream is “good.” While not part of the main parsing path, it’s handy
for tests and for graceful UX (e.g., letting a CLI wrapper detect missing
configs before calling parseFile). Keeping it const and self-contained avoids
side effects and aligns with the “pure read” nature of validation.
Because the real loader throws on failure, this method offers a non-throwing
way to check file presence when needed.

*/

bool ServerConfig::canOpen(const char *path) const
{
	std::ifstream f(path);
	return f.good();
}

/*

std::string ServerConfig::readWholeFile(const std::string& path)

Loads the entire configuration file into a single std::string using
binary I/O and a stream buffer copy, then returns it. If the file can’t
be opened, it throws a runtime_error with the offending path.
Centralizing all file I/O here keeps parseFile tiny and allows parseString
(the real parser entry) to be used by tests that feed in-memory configuration text.
Reading the full file first is practical because configs are small; it simplifies
tokenization (single pass on a contiguous buffer) and error reporting
(you can include positions based on the token stream if extended later).
The next stage, tokenize, consumes this returned string.


*/

std::string ServerConfig::readWholeFile(const std::string &path)
{
	std::ifstream f(path.c_str(), std::ios::in | std::ios::binary);
	if (!f)
	{
		throw std::runtime_error("cannot open config: " + path);
	}
	std::ostringstream oss;
	oss << f.rdbuf();
	return oss.str();
}

/*

std::vector<std::string> ServerConfig::tokenize(const std::string& data)

Turns raw configuration text into a flat token vector.
It removes #…\n comments, splits on whitespace, and emits {, },
and ; as standalone tokens. Everything else is accumulated verbatim
into the current token. This deliberately simple lexer mirrors the
NGINX style and avoids quoting/escaping complexity. By normalizing punctuation
as separate tokens, the grammar in parseTokens becomes straightforward
(lookahead like if (tok[i] == "{")). The output is the sole input to the
parser—keeping this function pure and deterministic makes unit testing easy (e.g.,
comments removed, accidental ; adjacency handled). Tokenizing first, then parsing,
also improves error handling because you can verify structure without
mixing I/O and grammar logic.


*/

std::vector<std::string> ServerConfig::tokenize(const std::string &data)
{
	std::vector<std::string> out;
	std::string cur;

	for (std::string::size_type i = 0; i < data.size();)
	{
		char ch = data[i];

		// comments start with '#': skip until newline
		if (ch == '#')
		{
			while (i < data.size() && data[i] != '\n')
				++i;
			continue;
		}

		// whitespace splits tokens
		if (std::isspace(static_cast<unsigned char>(ch)))
		{
			if (!cur.empty())
			{
				out.push_back(cur);
				cur.clear();
			}
			++i;
			continue;
		}

		// single-character tokens
		if (ch == '{' || ch == '}' || ch == ';')
		{
			if (!cur.empty())
			{
				out.push_back(cur);
				cur.clear();
			}
			out.push_back(std::string(1, ch));
			++i;
			continue;
		}

		// otherwise collect into current word
		cur.push_back(ch);
		++i;
	}

	if (!cur.empty())
		out.push_back(cur);
	return out;
}

/*

static std::string expectWord(std::size_t& i, const std::vector<std::string>& t)
static int expectInt(std::size_t& i, const std::vector<std::string>& t, const char* what)

These two local helpers keep the parser concise and robust.
expectWord advances the shared index i, returning the next token or throwing
“unexpected end” if the stream is exhausted. expectInt consumes one
token and parses a non-negative integer, with a few niceties: it strips an accidental trailing ;
defensively and supports NGINX-style size suffixes k/K and m/M for bytes (e.g., 10m → 10*1024*1024).
On invalid input (non-digits, overflow), it throws with a message that names the directive
(what) for clarity. These helpers are used throughout parseTokens for directives
like client_max_body_size, proxy timeouts, and rate limits, cutting repetition
nd making error messages consistent.


*/

namespace
{

	// next token as word (or throw)
	static std::string expectWord(std::size_t &i, const std::vector<std::string> &t)
	{
		if (i >= t.size())
			throw std::runtime_error("unexpected end");
		return t[i++];
	}

	// parse an unsigned integer, tolerating:
	//  - a trailing ';' that might have stuck to the number (defensive)
	//  - nginx-style size suffixes: 1k (1024), 10m (10*1024*1024)
	static int expectInt(std::size_t &i, const std::vector<std::string> &t, const char *what)
	{
		if (i >= t.size())
			throw std::runtime_error(std::string("missing number for ") + what);
		std::string s = t[i++];

		// strip an accidental trailing ';' (defensive)
		if (!s.empty() && s[s.size() - 1] == ';')
			s.erase(s.size() - 1);

		// support nginx-like suffixes k/m (bytes)
		long multiplier = 1;
		if (!s.empty())
		{
			char last = s[s.size() - 1];
			if (last == 'k' || last == 'K')
			{
				multiplier = 1024;
				s.erase(s.size() - 1);
			}
			else if (last == 'm' || last == 'M')
			{
				multiplier = 1024L * 1024L;
				s.erase(s.size() - 1);
			}
		}

		// digits only
		if (s.empty())
			throw std::runtime_error(std::string("invalid number for ") + what);
		long v = 0;
		for (std::string::size_type j = 0; j < s.size(); ++j)
		{
			if (s[j] < '0' || s[j] > '9')
				throw std::runtime_error(std::string("invalid number for ") + what);
			v = v * 10 + (s[j] - '0');
			if (v > LONG_MAX / 10)
				break; // avoid UB; rough guard
		}
		v *= multiplier;
		if (v < 0 || v > INT_MAX)
			throw std::runtime_error(std::string("number too large for ") + what);
		return static_cast<int>(v);
	}

}

/*


void ServerConfig::parseTokens(const std::vector<std::string>& tok)

This is the main grammar: it walks the flat token stream and builds
\the full configuration tree. Top-level sections include: (1) types { … }
to fill the MIME map; (2) global cgi lines to seed default CgiSpecs;
(3) session flags (enablement, cookie attributes); (4) global upstream pools; (5)
IP allow/deny blocks and defaults. For each server { … }, it records listen (host/port),
server_names, root, index, error_pages, client body settings, optional per-server upstream
overrides, and a list of location { … } blocks. Each location supports NGINX-like
knobs: root/index/autoindex, allowed methods, uploads (upload_dir, upload_store,
size/overwrite), CGI per extension, proxying, rate limits, try_files, synthetic
return, and per-location IP rules. The function throws on structural
errors (missing braces/semicolons) and invalid values, ensuring fail-fast
semantics before the server binds sockets. Its outputs feed Server (listeners),
Router (resolution), MIME/CGI registries, and request guards.


*/

namespace
{ // TU-local helpers (do not leak symbols)

	// ========== GLOBAL blocks/directives ==========


	/* 
	
		parseTypesBlock(ServerConfig&, const std::vector<std::string>&, std::size_t&)

		Parses the global types { … } block that maps file extensions to MIME types. 
		It validates that a { follows types, then loops until } reading pairs <ext> <mime> 
		and requiring a trailing ; per pair. Each mapping is inserted into ServerConfig::mime_mapping. 
		If the block is malformed (missing ;, missing closing brace), it throws with a precise message. 
		Centralizing this in a dedicated function keeps parseTokens small and makes it easy to extend 
		later (e.g., supporting includes or multi-word MIME types). At runtime, the static file handler 
		consults this map when crafting Content-Type, and CGI responses may use it as a fallback 
		if the CGI omits a type.
	
	*/

	static void parseTypesBlock(ServerConfig &sc, const std::vector<std::string> &tok, std::size_t &i)
	{
		const std::size_t N = tok.size();
		// pre: tok[i] == "types"
		if (i + 1 >= N || tok[i + 1] != "{")
			throw std::runtime_error("expected '{' after 'types'");
		i += 2; // skip "types" "{"
		while (i < N && tok[i] != "}")
		{
			const std::string ext = expectWord(i, tok);
			const std::string mime = expectWord(i, tok);
			if (i >= N || tok[i] != ";")
				throw std::runtime_error("expected ';' in types block");
			++i;
			sc.mime_mapping[ext] = mime;
		}
		if (i >= N || tok[i] != "}")
			throw std::runtime_error("missing '}' to close types");
		++i; // "}"
	}


	/* 
	
		parseGlobalCgi(ServerConfig&, const std::vector<std::string>&, std::size_t&)

		Handles global cgi lines: cgi .ext [binary] timeout_ms ;. 
		It supports two shapes: just a timeout (inherit interpreter elsewhere) 
		or both binary path and timeout. The parser probes the next token; 
		if it’s numeric, it’s a timeout; otherwise it treats it as a binary path 
		and parses the following timeout. It tolerates an optional trailing ;. 
		The result is stored as cgi_defaults[ext] = CgiSpec(bin, timeout). Later, 
		when a request targets *.py or *.php, the CGI registry will first check 
		a location-local map and then this global default to decide whether 
		to run a CGI and with which interpreter and deadline. Keeping the logic 
		here small and strict avoids ambiguity and ensures consistent 
		behavior across server and location scopes.
	
	*/

	static void parseGlobalCgi(ServerConfig &sc, const std::vector<std::string> &tok, std::size_t &i)
	{
		const std::size_t N = tok.size();
		// pre: tok[i] == "cgi"
		if (i + 2 >= N)
			throw std::runtime_error("cgi expects: .ext [binary] timeout_ms ;");

		const std::string ext = tok[i + 1];
		std::size_t adv = 2;
		std::string bin;
		int timeout = 0;

		if (i + 2 < N && tok[i + 2] != ";" && tok[i + 2] != "{" && tok[i + 2] != "}")
		{
			if (parseUnsigned(tok[i + 2], timeout))
			{
				adv = 3; // cgi .php 3000 ;
			}
			else
			{
				bin = tok[i + 2];
				adv = 3; // cgi .php /path/to/bin 3000 ;
				if (i + 3 < N)
				{
					if (!parseUnsigned(tok[i + 3], timeout))
						throw std::runtime_error("cgi timeout must be integer");
					adv = 4;
				}
			}
		}
		if (i + adv < N && tok[i + adv] == ";")
			++adv;

		sc.cgi_defaults[ext] = CgiSpec(bin, timeout);
		i += adv; // position after directive
	}



	/* 
	
		parseSessionDirective(ServerConfig&, const std::vector<std::string>&, std::size_t&)

		Dispatches and parses the family of session directives: 
		the master session on|off, session_cookie_name, session_max_age, 
		session_secure, session_http_only, and session_same_site. 
		It advances the token cursor appropriately, tolerating 
		optional trailing semicolons, and sets the corresponding 
		fields on ServerConfig. Keeping all session toggles in one helper 
		keeps parseTokens tidy and ensures error messages are consistent 
		(e.g., “expects on|off” for boolean toggles). These values are later 
		consulted by response builders when setting or refreshing session 
		cookies (e.g., adding Secure, HttpOnly, SameSite attributes). 
		The function is read-only with respect to structure, so it 
		fails fast only on missing values or invalid boolean flags.
	
	
	*/

	static void parseSessionDirective(ServerConfig &sc, const std::vector<std::string> &tok, std::size_t &i)
	{
		const std::size_t N = tok.size();
		const std::string &t0 = tok[i];

		if (t0 == "session")
		{
			if (i + 1 >= N)
				throw std::runtime_error("session on|off");
			const std::string v = tok[i + 1];
			if (v == "on")
				sc.session_enabled = true;
			else if (v == "off")
				sc.session_enabled = false;
			else
				throw std::runtime_error("session expects on|off");
			i += (i + 2 < N && tok[i + 2] == ";") ? 3 : 2;
			return;
		}
		if (t0 == "session_cookie_name")
		{
			sc.session_cookie_name = tok[i + 1];
			i += (i + 2 < N && tok[i + 2] == ";") ? 3 : 2;
			return;
		}
		if (t0 == "session_max_age")
		{
			sc.session_max_age = expectInt(++i, tok, "session_max_age");
			if (i < N && tok[i] == ";")
				++i;
			return;
		}
		if (t0 == "session_secure")
		{
			const std::string v = tok[i + 1];
			sc.session_secure = (v == "on");
			i += (i + 2 < N && tok[i + 2] == ";") ? 3 : 2;
			return;
		}
		if (t0 == "session_http_only")
		{
			const std::string v = tok[i + 1];
			sc.session_http_only = (v == "on");
			i += (i + 2 < N && tok[i + 2] == ";") ? 3 : 2;
			return;
		}
		if (t0 == "session_same_site")
		{
			sc.session_same_site = tok[i + 1];
			i += (i + 2 < N && tok[i + 2] == ";") ? 3 : 2;
			return;
		}

		// Should not reach here if dispatcher is correct
		throw std::runtime_error("unknown session directive");
	}


	/* 
	
	parseUpstreamPoolBody(UpstreamPool&, const std::vector<std::string>&, std::size_t&)

	Parses the body of an upstream { … } block used both at global 
	scope and inside a server. It accepts multiple server host:port [weight=N]; 
	entries plus optional options like health_path, health_interval_ms, and strategy, 
	each with optional trailing semicolons. Every backend is added to pool.nodes 
	with default weight=1 and healthy=true unless overridden. By isolating the common 
	grammar here, higher-level functions parseGlobalUpstream and parsePerServerUpstream 
	can reuse it, reducing duplication. At runtime, a named UpstreamPool informs the proxy 
	handler how to pick and connect to backends (strategy, health checking route, weights).
	
	
	*/

	static void parseUpstreamPoolBody(UpstreamPool &pool, const std::vector<std::string> &tok, std::size_t &i)
	{
		const std::size_t N = tok.size();
		while (i < N && tok[i] != "}")
		{
			const std::string kw = tok[i++];
			if (kw == ";")
				continue; // tolerate stray semicolons

			if (kw == "server")
			{
				if (i >= N)
					throw std::runtime_error("upstream server expects host:port");
				const std::string hp = tok[i++];
				std::string::size_type c = hp.find(':');
				if (c == std::string::npos)
					throw std::runtime_error("server expects host:port");

				const std::string host = hp.substr(0, c);
				const std::string pstr = hp.substr(c + 1);
				int port;
				if (!parseUnsigned(pstr, port) || port <= 0 || port > 65535)
					throw std::runtime_error("bad port");

				int weight = 1;
				while (i < N && tok[i] != ";")
				{
					const std::string kv = tok[i++];
					const std::string::size_type eq = kv.find('=');
					if (eq != std::string::npos && kv.substr(0, eq) == "weight")
					{
						int w;
						if (!parseUnsigned(kv.substr(eq + 1), w) || w <= 0)
							throw std::runtime_error("bad weight");
						weight = w;
					}
				}
				if (i >= N || tok[i] != ";")
					throw std::runtime_error("expected ';' after upstream server");
				++i;

				Upstream u;
				u.host = host;
				u.port = port;
				u.weight = weight;
				u.healthy = true;
				pool.nodes.push_back(u);
				continue;
			}
			if (kw == "health_path")
			{
				pool.health_path = expectWord(i, tok);
				if (i < N && tok[i] == ";")
					++i;
				continue;
			}
			if (kw == "health_interval_ms")
			{
				pool.health_interval_ms = expectInt(i, tok, "health_interval_ms");
				if (i < N && tok[i] == ";")
					++i;
				continue;
			}
			if (kw == "strategy")
			{
				pool.strategy = expectWord(i, tok);
				if (i < N && tok[i] == ";")
					++i;
				continue;
			}
			throw std::runtime_error("unknown directive in upstream block: " + kw);
		}
		if (i >= tok.size() || tok[i] != "}")
			throw std::runtime_error("missing '}' in upstream");
		++i; // "}"
	}



	/* 
	
		parseGlobalUpstream(ServerConfig&, const std::vector<std::string>&, std::size_t&)

		Parses the global form upstream NAME { … }. It validates the shape, captures NAME, 
		calls parseUpstreamPoolBody to fill an UpstreamPool, and then stores it in ServerConfig::upstream_pools[NAME]. 
		Global pools provide shared backend definitions that server blocks can inherit automatically 
		(your code copies upstream_pools into each VirtualServer at creation). Locations that enable 
		proxying (proxy_pass NAME;) then reference these pools by name. Keeping globals and overrides s
		eparate makes it easy to define common backends while still allowing site-specific 
		customizations later inside a server block.
	
	
	*/

	static void parseGlobalUpstream(ServerConfig &sc, const std::vector<std::string> &tok, std::size_t &i)
	{
		const std::size_t N = tok.size();
		// pre: tok[i] == "upstream"
		if (i + 2 >= N || tok[i + 2] != "{")
			throw std::runtime_error("upstream NAME { ... }");
		const std::string upname = tok[i + 1];
		i += 3; // skip "upstream" NAME "{"
		UpstreamPool pool;
		parseUpstreamPoolBody(pool, tok, i);
		sc.upstream_pools[upname] = pool;
	}


	/* 
	
		parseAllowIpBlock(ServerConfig&, const std::vector<std::string>&, std::size_t&)

		Parses a global allowIp { … } ACL list. It validates the opening 
		{, then loops tokens until } and passes each CIDR or address string 
		to ip_list.addAllowRule. If syntax is bad, it throws. Finally, 
		it requires and consumes the closing }. This block sets process-wide 
		allow rules that are consulted during connection acceptance or early 
		request handling to drop unwanted clients quickly. By validating structure 
		and addresses at load time, the runtime can enforce access efficiently 
		without additional parsing cost.
	
	*/

	static void parseAllowIpBlock(ServerConfig &sc, const std::vector<std::string> &tok, std::size_t &i)
	{
		const std::size_t N = tok.size();
		// pre: tok[i] == "allowIp"
		if (i + 2 >= N || tok[i + 1] != "{")
			throw std::runtime_error("allowIp expects: { ADRESS/CIDR ... }");
		i += 2; // skip "allowIp" "{"
		while (i < N && tok[i] != "}")
		{
			if (!sc.ip_list.addAllowRule(tok[i]))
				throw std::runtime_error("allowed IP has invalid syntax");
			++i;
		}
		if (i >= N || tok[i] != "}")
			throw std::runtime_error("allowIp expects: { ADRESS/CIDR ... }");
		++i; // "}"
	}



	/* 
	
		parseDenyIpBlock(ServerConfig&, const std::vector<std::string>&, std::size_t&)

		Mirror of parseAllowIpBlock, but for denyIp { … }. 
		It adds each address/CIDR to the deny rules via ip_list.addDenyRule. 
		With both allow and deny lists, plus defaultAllowIp, the server can implement 
		common ACL policies (e.g., default allow except a few blocks, 
		or default deny with specific allows). Parsing and validating here 
		avoids surprises during traffic spikes and ensures the rule set 
		is well-formed before the server starts accepting connections.
	
	*/

	static void parseDenyIpBlock(ServerConfig &sc, const std::vector<std::string> &tok, std::size_t &i)
	{
		const std::size_t N = tok.size();
		// pre: tok[i] == "denyIp"
		if (i + 2 >= N || tok[i + 1] != "{")
			throw std::runtime_error("denyIp expects: { ADRESS/CIDR ... }");
		i += 2; // skip "denyIp" "{"
		while (i < N && tok[i] != "}")
		{
			if (!sc.ip_list.addDenyRule(tok[i]))
				throw std::runtime_error("denied IP has invalid syntax");
			++i;
		}
		if (i >= N || tok[i] != "}")
			throw std::runtime_error("denyIp expects: { ADRESS/CIDR ... }");
		++i; // "}"
	}


	/* 
	
		parseDefaultAllowIp(ServerConfig&, const std::vector<std::string>&, std::size_t&)

		Parses the default policy switch defaultAllowIp true|false ;. 
		It advances past the directive token, reads the boolean keyword, 
		sets ip_list.defAllow accordingly, and leaves semicolon handling 
		to the caller loop. This single setting determines the fallback 
		when a client IP matches neither allow nor deny lists. 
		Keeping the toggle explicit in config (rather than implicit) 
		makes the server’s access behavior predictable and easy to audit.
	
	*/

	static void parseDefaultAllowIp(ServerConfig &sc, const std::vector<std::string> &tok, std::size_t &i)
	{
		const std::size_t N = tok.size();
		// pre: tok[i] == "defaultAllowIp"
		if (i + 2 >= N)
			throw std::runtime_error("defaultAllowIp expects: true/false/banana ;");
		++i; // move to value
		if (tok[i] == "true")
			sc.ip_list.defAllow = true;
		else
			sc.ip_list.defAllow = false;
		++i; // position after value (semicolon tolerance handled by caller loop)
	}

	// ========== SERVER-level ==========


	/* 
	
	parseListen(VirtualServer&, const std::vector<std::string>&, std::size_t&)

	Parses listen in the forms listen PORT;, listen HOST:PORT;, or listen HOST PORT;. 
	It carefully validates the port range (1–65535), extracts an optional host 
	(empty means wildcard), tolerates an optional trailing ;, and writes results into vs.
	listen_host and vs.listen_port. Because binding is one of the few operations that can 
	fail for external reasons, the parser ensures the syntax is correct and values are 
	sensible so that any later error is truly environmental (e.g., permission, address in use). 
	This directive is required; parseServerBlock will refuse to accept a server 
	without a valid listen specification, preventing ambiguous startup.
	
	
	*/

	static void parseListen(VirtualServer &vs, const std::vector<std::string> &tok, std::size_t &i)
	{
		const std::size_t N = tok.size();
		if (i >= N)
			throw std::runtime_error("listen expects 'PORT' or 'HOST:PORT'");

		std::string a = tok[i++], b;
		if (i < N && tok[i] != ";" && tok[i] != "{" && tok[i] != "}")
			b = tok[i++];

		std::string host;
		int port = -1;

		if (b.empty())
		{
			std::string::size_type cpos = a.find(':');
			if (cpos == std::string::npos)
			{
				char *endp = 0;
				long p = std::strtol(a.c_str(), &endp, 10);
				if (!a.size() || (endp && *endp) || p <= 0 || p > 65535)
					throw std::runtime_error("invalid listen port");
				port = static_cast<int>(p);
				host.clear();
			}
			else
			{
				host = a.substr(0, cpos);
				std::string pstr = a.substr(cpos + 1);
				char *endp = 0;
				long p = std::strtol(pstr.c_str(), &endp, 10);
				if (!pstr.size() || (endp && *endp) || p <= 0 || p > 65535)
					throw std::runtime_error("invalid listen port");
				port = static_cast<int>(p);
			}
		}
		else
		{
			host = a;
			char *endp = 0;
			long p = std::strtol(b.c_str(), &endp, 10);
			if (!b.size() || (endp && *endp) || p <= 0 || p > 65535)
				throw std::runtime_error("invalid listen port");
			port = static_cast<int>(p);
		}

		if (i < N && tok[i] == ";")
			++i;

		vs.listen_host = host;
		vs.listen_port = port;
	}


	/* 
	
		parseServerName(VirtualServer&, const std::vector<std::string>&, std::size_t&)

		Collects one or more server_name tokens until ; 
		and appends them to vs.server_names. 
		While your current runtime may not fully leverage Host: header routing, 
		capturing these names now keeps your config compatible with typical NGINX usage and makes 
		it easy to add name-based virtual hosting later. The helper enforces 
		the terminating semicolon and provides a clear error if it’s missing, 
		aiding fast feedback during edits.
	
	
	*/

	static void parseServerName(VirtualServer &vs, const std::vector<std::string> &tok, std::size_t &i)
	{
		const std::size_t N = tok.size();
		while (i < N && tok[i] != ";")
			vs.server_names.push_back(tok[i++]);
		if (i >= N || tok[i] != ";")
			throw std::runtime_error("expected ';'");
		++i;
	}


	/* 
	
	parseRoot(VirtualServer&, const std::vector<std::string>&, std::size_t&)

	Parses root <path>; at the server level, normalizes the path 
	to absolute with makeAbsolute, and stores it in vs.root. 
	The trailing semicolon is required. This root acts as the default 
	document root for static file resolution under this virtual server 
	unless overridden by a location. Normalizing early ensures static 
	lookups and error page loads perform against unambiguous paths, 
	avoiding surprises if chdir or relative invocations are used.
	
	
	*/


	static void parseRoot(VirtualServer &vs, const std::vector<std::string> &tok, std::size_t &i)
	{
		const std::size_t N = tok.size();
		if (i >= N)
			throw std::runtime_error("root expects path");
		vs.root = makeAbsolute(tok[i++]);
		if (i >= N || tok[i] != ";")
			throw std::runtime_error("expected ';' after root");
		++i;
	}



	/* 

		parseIndex(VirtualServer&, const std::vector<std::string>&, std::size_t&)

		Parses one or more index filenames until ; and records them in vs.index_files. 
		This sequence defines the fallback order when a request targets a directory: 
		the static resolver will try each listed index in order (e.g., index.html, then index.htm). 
		Keeping this list configurable per server (and overrideable per location) 
		matches common web server behavior and provides flexibility for different sites.
	
	
	*/

	static void parseIndex(VirtualServer &vs, const std::vector<std::string> &tok, std::size_t &i)
	{
		const std::size_t N = tok.size();
		while (i < N && tok[i] != ";")
			vs.index_files.push_back(tok[i++]);
		if (i >= N || tok[i] != ";")
			throw std::runtime_error("expected ';'");
		++i;
	}


	/* 
	
		parseErrorPage(VirtualServer&, const std::vector<std::string>&, std::size_t&)

		Parses error_page <status> <path> [;]. It validates the status with validateErrorStatusOrThrow, 
		converts it to an integer, captures the path token, tolerates the semicolon, and then stores 
		vs.error_pages[status] = path. At runtime, when a handler raises an error, 
		the response factory checks this map and, if present, serves the configured 
		page instead of a default. Centralizing the parse here keeps server scope 
		clean and maintains consistent validation for each mapping
	
	*/

	static void parseErrorPage(VirtualServer &vs, const std::vector<std::string> &tok, std::size_t &i)
	{
		const std::size_t N = tok.size();
		if (i + 1 >= N)
			throw std::runtime_error("error_page <status> <path>");
		validateErrorStatusOrThrow(tok[i]);
		int status = ::atoi(tok[i++].c_str());
		const std::string path = tok[i++];
		if (i < N && tok[i] == ";")
			++i;
		vs.error_pages[status] = path;
	}


	/* 
	
	parseClientBodyTempPath(VirtualServer&, const std::vector<std::string>&, std::size_t&)

	Parses client_body_temp_path <path> [;], normalizes the path, 
	and stores it in vs.client_body_temp_path. Large request bodies 
	(uploads, big POST/PUT) may spill to disk; this directive tells 
	the server where such temporary files should live. Performing normalization 
	here ensures subsequent I/O uses absolute locations and that 
	permissions issues can be debugged with clear paths.
	
	*/

	static void parseClientBodyTempPath(VirtualServer &vs, const std::vector<std::string> &tok, std::size_t &i)
	{
		const std::size_t N = tok.size();
		if (i >= N)
			throw std::runtime_error("client_body_temp_path <path>");
		vs.client_body_temp_path = makeAbsolute(tok[i++]);
		if (i < N && tok[i] == ";")
			++i;
	}


	/* 
	
		parseClientMaxBodySize(VirtualServer&, const std::vector<std::string>&, std::size_t&)

		Parses client_max_body_size <size>[k|m] [;] using expectInt so human-friendly suffixes work. 
		It sets vs.client_max_body_size for enforcement in body readers and request guards. 
		Placing this at server scope allows per-site policies that can then be overridden 
		in locations where needed (e.g., larger limits for upload endpoints). 
		Early validation prevents starting with nonsensical or negative sizes.
	
	
	*/

	static void parseClientMaxBodySize(VirtualServer &vs, const std::vector<std::string> &tok, std::size_t &i)
	{
		vs.client_max_body_size = expectInt(i, tok, "client_max_body_size");
		if (i < tok.size() && tok[i] == ";")
			++i;
	}


	/* 

	parsePerServerUpstream(VirtualServer&, const std::vector<std::string>&, std::size_t&)

	Parses a server-scoped override upstream NAME { … }. 
	After validating the {, it calls parseUpstreamPoolBody into a fresh UpstreamPool and assigns it to vs.upstreams[NAME], 
	shadowing any global pool of the same name. This allows each virtual server to customize its 
	upstream topology (different hosts, weights, health paths) while still inheriting global defaults 
	for other names. The proxy handler later resolves proxy_pass 
	NAME against this per-server map first, then (if you choose) global.
	
	*/

	static void parsePerServerUpstream(VirtualServer &vs, const std::vector<std::string> &tok, std::size_t &i)
	{
		// pre: current token is NAME, next must be "{"
		const std::size_t N = tok.size();
		if (i + 1 >= N || tok[i + 1] != "{")
			throw std::runtime_error("upstream NAME { ... }");
		const std::string upname = tok[i++]; // NAME
		++i;								 // skip "{"
		UpstreamPool pool;
		// Reuse common upstream body parser:
		parseUpstreamPoolBody(pool, tok, i);
		vs.upstreams[upname] = pool;
	}

	// ========== LOCATION-level ==========


	/* 
		parseLocRoot(Location&, const std::vector<std::string>&, std::size_t&)

		Parses root <path>; within a location, normalizes the path, and stores it as loc.root. 
		This overrides the server root for requests matching the location prefix/regex. 
		It must end with ;. Having per-location roots is crucial 
		for building sub-sites or mounting specific directories under a common host.
	
	*/

	static void parseLocRoot(Location &loc, const std::vector<std::string> &tok, std::size_t &i)
	{
		loc.root = makeAbsolute(expectWord(i, tok));
		if (i >= tok.size() || tok[i] != ";")
			throw std::runtime_error("expected ';' after root");
		++i;
	}


	/* 
	
	parseLocIndex(Location&, const std::vector<std::string>&, std::size_t&)

	Collects one or more index filenames until ; 
	and stores them in loc.index_files. This list overrides the server-level 
	index order for this route. The static handler will use the most specific 
	available policy (location first, then server) to decide which index file to try.
	
	*/

	static void parseLocIndex(Location &loc, const std::vector<std::string> &tok, std::size_t &i)
	{
		const std::size_t N = tok.size();
		while (i < N && tok[i] != ";")
			loc.index_files.push_back(tok[i++]);
		if (i >= N || tok[i] != ";")
			throw std::runtime_error("expected ';'");
		++i;
	}

	/* 
	
	parseLocAutoindex(Location&, const std::vector<std::string>&, std::size_t&)

	Parses autoindex on|off [;] and toggles loc.autoindex. 
	When no index file exists for a directory request, on allows directory listings, 
	while off makes the handler return 403/404 per your overall policy. 
	Allowing configuration at location scope enables you to expose 
	listing on specific paths only (e.g., a public downloads folder).
	
	
	*/

	static void parseLocAutoindex(Location &loc, const std::vector<std::string> &tok, std::size_t &i)
	{
		const std::string v = expectWord(i, tok);
		if (v == "on")
			loc.autoindex = true;
		else if (v == "off")
			loc.autoindex = false;
		else
			throw std::runtime_error("autoindex on|off");
		if (i < tok.size() && tok[i] == ";")
			++i;
	}


	/* 
	
		parseLocMethods(Location&, const std::vector<std::string>&, std::size_t&)

		Parses methods GET|POST|DELETE;, validating that only supported verbs appear. 
		It collects them into loc.allowed_methods. Request guards use this list 
		to short-circuit disallowed methods with a 405 response before doing 
		any expensive work (disk, CGI, or proxy). Keeping it per-location 
		allows tight control (e.g., only GET for static directories; 
		POST allowed on /upload).
	
	
	*/

	static void parseLocMethods(Location &loc, const std::vector<std::string> &tok, std::size_t &i)
	{
		const std::size_t N = tok.size();
		while (i < N && tok[i] != ";")
		{
			const std::string m = tok[i++];
			if (!(m == "GET" || m == "POST" || m == "DELETE"))
				throw std::runtime_error("unsupported method");
			loc.allowed_methods.push_back(m);
		}
		if (i >= N || tok[i] != ";")
			throw std::runtime_error("expected ';'");
		++i;
	}


	/* 
	
		parseLocUploadDir(Location&, const std::vector<std::string>&, std::size_t&)

		Parses a legacy/alternate upload_dir <path> [;], normalizes it, 
		and stores it in loc.upload_dir. Depending on your codebase, 
		upload_store may be the primary directive; this helper preserves 
		compatibility with prior configs while letting the upload handler 
		decide which field to honor. Normalization ensures consistent, 
		absolute paths for writes.
	
	*/

	static void parseLocUploadDir(Location &loc, const std::vector<std::string> &tok, std::size_t &i)
	{
		loc.upload_dir = makeAbsolute(expectWord(i, tok));
		if (i < tok.size() && tok[i] == ";")
			++i;
	}


	/* 
	
		parseLocCgi(Location&, const std::vector<std::string>&, std::size_t&)

		Parses a location-scoped cgi .ext [bin] [timeout] [;]. 
		It supports either a bare timeout or a bin plus timeout. 
		The result is stored in loc.cgi_by_ext[ext] = CgiSpec(bin, to). 
		At runtime, the CGI registry first checks loc.cgi_by_ext, then 
		falls back to global defaults. This enables enabling/disabling 
		or changing interpreter/timeouts per route—for example, allowing .py 
		only under /cgi/ and not elsewhere.
	
	*/

	static void parseLocCgi(Location &loc, const std::vector<std::string> &tok, std::size_t &i)
	{
		const std::size_t N = tok.size();
		const std::string ext = expectWord(i, tok);
		std::string bin;
		int to = 0;
		if (i < N && tok[i] != ";" && tok[i] != "}")
		{
			if (!parseUnsigned(tok[i], to))
			{
				bin = tok[i++];
				to = expectInt(i, tok, "cgi timeout");
			}
			else
			{
				to = std::atoi(tok[i++].c_str());
			}
		}
		if (i < N && tok[i] == ";")
			++i;
		loc.cgi_by_ext[ext] = CgiSpec(bin, to);
	}


	/* 

		parseLocProxy(Location&, const std::vector<std::string>&, std::size_t&, const std::string& lkw)

		Handles proxy_pass NAME;, proxy_connect_timeout ms;, 
		and proxy_read_timeout ms;. For proxy_pass, it marks loc.is_proxy = true 
		and records the named upstream pool to use. The timeouts set connection 
		and idle I/O windows, respectively, and are enforced by the proxy handler 
		to avoid hangs. Having these knobs at location scope lets you proxy 
		only specific paths to backends and tune timeouts per service.
	
	*/

static void parseLocProxy(Location &loc,
                          const std::vector<std::string> &tok,
                          std::size_t &i,
                          const std::string &lkw)
{
    // location ... { proxy_pass NAME; proxy_connect_timeout 2000; proxy_read_timeout 5000; }
    // - We only *store* the pool name and timeouts here.
    // - Resolution (NAME -> concrete host:port) is done at runtime by RouteResolver.

    if (lkw == "proxy_pass")
    {
        // Prevent accidental duplicates (clearer errors during config authoring)
        if (loc.is_proxy && !loc.proxy_name.empty())
            throw std::runtime_error("duplicate proxy_pass in the same location");

        loc.is_proxy = true;
        loc.proxy_name = expectWord(i, tok);          // pool name (e.g., "backend")
        if (i < tok.size() && tok[i] == ";") ++i;     // tolerate optional trailing ';'
        return;
    }

    if (lkw == "proxy_connect_timeout")
    {
        // Milliseconds to wait for TCP connect to upstream
        loc.proxy_connect_timeout_ms = expectInt(i, tok, "proxy_connect_timeout");
        if (i < tok.size() && tok[i] == ";") ++i;     // optional ';'
        return;
    }

    if (lkw == "proxy_read_timeout")
    {
        // I/O idle timeout once connected (read/write inactivity)
        loc.proxy_io_idle_timeout_ms = expectInt(i, tok, "proxy_read_timeout");
        if (i < tok.size() && tok[i] == ";") ++i;     // optional ';'
        return;
    }

    // Anything else is a config error inside a `location` proxy section
    throw std::runtime_error("unknown proxy directive in location");
}


	/* 
	
		parseLocLimitReq(Location&, const std::vector<std::string>&, std::size_t&)

		Parses limit_req in a compact, flag-style form such as limit_req requests=60 burst=20 on;. 
		It scans tokens until ;, extracts numeric requests (rate) 
		and burst parameters, and toggles an enabled flag. The result populates loc.rate_limit. 
		During request handling, guards consult this structure to throttle 
		abusive clients per route, shedding load safely without affecting unrelated paths.
	
	
	*/

	static void parseLocLimitReq(Location &loc, const std::vector<std::string> &tok, std::size_t &i)
	{
		const std::size_t N = tok.size();
		int reqs = 0, burst = 0;
		bool haveReq = false, haveBurst = false;
		bool enabled = true;
		while (i < N && tok[i] != ";")
		{
			const std::string kv = tok[i++];
			std::string::size_type e = kv.find('=');
			if (e != std::string::npos)
			{
				const std::string k = kv.substr(0, e), v = kv.substr(e + 1);
				int num;
				if (!parseUnsigned(v, num))
					throw std::runtime_error("limit_req expects numbers");
				if (k == "requests")
				{
					reqs = num;
					haveReq = true;
				}
				else if (k == "burst")
				{
					burst = num;
					haveBurst = true;
				}
			}
			else if (kv == "on")
			{
				enabled = true;
			}
			else if (kv == "off")
			{
				enabled = false;
			}
		}
		if (i >= N || tok[i] != ";")
			throw std::runtime_error("expected ';'");
		++i;

		loc.rate_limit.requests_per_minute = haveReq ? reqs : 0;
		loc.rate_limit.burst = haveBurst ? burst : 0;
		loc.rate_limit.enabled = enabled;
	}


	/* 
	
		parseLocPutPatch(Location&, const std::vector<std::string>&, std::size_t&, const std::string& lkw)

		Parses allow_put on|off; and allow_patch on|off;, setting flags in loc.write_conf. 
		These determine whether the PutPatchHandler will accept idempotent writes to resources 
		under this route. Keeping these toggles local lets you enable writes precisely 
		where intended while leaving the rest of the site read-only.
	
	*/

	static void parseLocPutPatch(Location &loc, const std::vector<std::string> &tok, std::size_t &i, const std::string &lkw)
	{
		const std::string v = expectWord(i, tok);
		if (lkw == "allow_put")
			loc.write_conf.allow_put = (v == "on");
		if (lkw == "allow_patch")
			loc.write_conf.allow_patch = (v == "on");
		if (i < tok.size() && tok[i] == ";")
			++i;
	}


	/* 
	
		parseLocEtagOrMax(Location&, const std::vector<std::string>&, std::size_t&, const std::string& lkw)

		Handles generate_etag on|off; and location-scoped client_max_body_size <size>;. 
		The first toggles whether the server synthesizes ETags for write responses 
		or static content under this route (depending on your implementation). 
		The second constrains request body size at the route level, overriding the server default. 
		Centralizing both small knobs here avoids clutter in the main location loop.
	
	
	*/

	static void parseLocEtagOrMax(Location &loc, const std::vector<std::string> &tok, std::size_t &i, const std::string &lkw)
	{
		if (lkw == "generate_etag")
		{
			const std::string v = expectWord(i, tok);
			loc.write_conf.generate_etag = (v == "on");
			if (i < tok.size() && tok[i] == ";")
				++i;
			return;
		}
		if (lkw == "client_max_body_size")
		{
			loc.write_conf.max_body_bytes = expectInt(i, tok, "client_max_body_size");
			if (i < tok.size() && tok[i] == ";")
				++i;
			return;
		}
		throw std::runtime_error("unknown write directive in location");
	}



	/* 

		parseLocTryFiles(Location&, const std::vector<std::string>&, std::size_t&)

		Parses try_files lists until ;, collecting candidate paths and optional 
		final forms like =404. At runtime, the static resolver will attempt each candidate 
		in order, substituting variables (if supported) or falling back to the explicit code. 
		This NGINX-style mechanism provides fine-grained control over fallback behavior 
		without writing a CGI or proxy.
	
	*/

	static void parseLocTryFiles(Location &loc, const std::vector<std::string> &tok, std::size_t &i)
	{
		const std::size_t N = tok.size();
		while (i < N && tok[i] != ";")
			loc.try_files.push_back(tok[i++]);
		if (i >= N || tok[i] != ";")
			throw std::runtime_error("expected ';'");
		++i;
	}


	/* 
	
		parseLocReturn(Location&, const std::vector<std::string>&, std::size_t&)

		Parses return CODE [text|uri] [;]. It sets loc.return_status and concatenates 
		remaining tokens until ;/} into loc.return_target. The router short-circuits 
		requests for this location to a synthetic response (e.g., return 301 /new; 
		or return 200 "Hello";). It’s a powerful escape hatch for simple redirects 
		or text responses without engaging static/CGI/proxy.
	
	*/

	static void parseLocReturn(Location &loc, const std::vector<std::string> &tok, std::size_t &i)
	{
		const std::size_t N = tok.size();
		loc.return_status = expectInt(i, tok, "return");
		while (i < N && tok[i] != ";" && tok[i] != "}")
		{
			loc.return_target += tok[i++];
			if (i < N && tok[i] != ";" && tok[i] != "}")
				loc.return_target += " ";
		}
		if (i < N && tok[i] == ";")
			++i;
	}


	/* 

		parseLocAcl(Location&, const std::vector<std::string>&, std::size_t&, const std::string& lkw)

		Parses allow <CIDR>; and deny <CIDR>; entries inside a location, pushing them into loc.allow_list/deny_list. 
		During request handling, these refine or override the global ACLs for that specific route, 
		enabling internal subtrees (e.g., only LAN) or blocking sensitive paths externally.

	*/

	static void parseLocAcl(Location &loc, const std::vector<std::string> &tok, std::size_t &i, const std::string &lkw)
	{
		if (lkw == "allow")
		{
			loc.allow_list.push_back(expectWord(i, tok));
			if (i < tok.size() && tok[i] == ";")
				++i;
			return;
		}
		if (lkw == "deny")
		{
			loc.deny_list.push_back(expectWord(i, tok));
			if (i < tok.size() && tok[i] == ";")
				++i;
			return;
		}
		throw std::runtime_error("unknown ACL directive in location");
	}


	/* 

	parseLocUpload(Location&, const std::vector<std::string>&, std::size_t&, const std::string& lkw)

	Handles upload_store <path>;, upload_overwrite on|off;, and upload_max_file_size <size>;. 
	It normalizes paths and validates numbers, writing into the location’s upload configuration. 
	The upload handler then uses these settings to stream multipart/form bodies to disk, 
	enforce per-route size limits, and decide whether overwriting existing files is allowed. 
	Keeping all upload controls local lets you safely expose only selected paths for user uploads.
	
	*/

	static void parseLocUpload(Location &loc, const std::vector<std::string> &tok, std::size_t &i, const std::string &lkw)
	{
		const std::size_t N = tok.size();
		if (lkw == "upload_store")
		{
			loc.upload_store = makeAbsolute(expectWord(i, tok));
			if (i < N && tok[i] == ";")
				++i;
			return;
		}
		if (lkw == "upload_overwrite")
		{
			const std::string v = expectWord(i, tok);
			if (v == "on")
				loc.upload_overwrite = true;
			else if (v == "off")
				loc.upload_overwrite = false;
			else
				throw std::runtime_error("upload_overwrite must be on|off");
			if (i < N && tok[i] == ";")
				++i;
			return;
		}
		if (lkw == "upload_max_file_size")
		{
			loc.upload_max_file_size = expectInt(i, tok, "upload_max_file_size");
			if (i < N && tok[i] == ";")
				++i;
			return;
		}
		throw std::runtime_error("unknown upload directive in location");
	}

	static void parseLocationBlock(VirtualServer &vs, const std::vector<std::string> &tok, std::size_t &i)
	{
		const std::size_t N = tok.size();
		if (i >= N)
			throw std::runtime_error("location expects <path|~ regex> {");

		Location loc;
		if (tok[i] == "~")
		{
			++i;
			if (i >= N)
				throw std::runtime_error("location ~ expects a pattern");
			loc.regex = true;
			loc.path_prefix = tok[i++];
		}
		else
		{
			loc.path_prefix = tok[i++];
		}

		if (i >= N || tok[i] != "{")
			throw std::runtime_error("expected '{' after location");
		++i;

		while (i < N && tok[i] != "}")
		{
			const std::string lkw = tok[i++];

			if (lkw == ";")
				continue;

			if (lkw == "root")
			{
				parseLocRoot(loc, tok, i);
				continue;
			}
			if (lkw == "index")
			{
				parseLocIndex(loc, tok, i);
				continue;
			}
			if (lkw == "autoindex")
			{
				parseLocAutoindex(loc, tok, i);
				continue;
			}
			if (lkw == "methods")
			{
				parseLocMethods(loc, tok, i);
				continue;
			}
			if (lkw == "upload_dir")
			{
				parseLocUploadDir(loc, tok, i);
				continue;
			}
			if (lkw == "cgi")
			{
				parseLocCgi(loc, tok, i);
				continue;
			}

			if (lkw == "proxy_pass" ||
				lkw == "proxy_connect_timeout" ||
				lkw == "proxy_read_timeout")
			{
				parseLocProxy(loc, tok, i, lkw);
				continue;
			}

			if (lkw == "limit_req")
			{
				parseLocLimitReq(loc, tok, i);
				continue;
			}

			if (lkw == "allow_put" ||
				lkw == "allow_patch")
			{
				parseLocPutPatch(loc, tok, i, lkw);
				continue;
			}

			if (lkw == "generate_etag" ||
				lkw == "client_max_body_size")
			{
				parseLocEtagOrMax(loc, tok, i, lkw);
				continue;
			}

			if (lkw == "try_files")
			{
				parseLocTryFiles(loc, tok, i);
				continue;
			}
			if (lkw == "return")
			{
				parseLocReturn(loc, tok, i);
				continue;
			}

			if (lkw == "allow" || lkw == "deny")
			{
				parseLocAcl(loc, tok, i, lkw);
				continue;
			}

			if (lkw == "upload_store" ||
				lkw == "upload_overwrite" ||
				lkw == "upload_max_file_size")
			{
				parseLocUpload(loc, tok, i, lkw);
				continue;
			}

			throw std::runtime_error(std::string("unknown directive in location: ") + lkw);
		}

		if (i >= N || tok[i] != "}")
			throw std::runtime_error("missing '}' to close location block");
		++i; // "}"

		vs.locations.push_back(loc);
	}


	/* 
	
		parseServerBlock(ServerConfig&, const std::vector<std::string>&, std::size_t&)

		Parses an entire server { … } block. It requires { after server, constructs a fresh 
		VirtualServer, and copies global upstream pools as defaults. Inside, it loops tokens until }, 
		dispatching to child helpers for each directive: listen, server_name, root, index, error_page, 
		body temp path/size, optional per-server upstream NAME { … }, and nested location blocks. 
		Unknown directives cause immediate errors. On close, it verifies that a valid listen was 
		provided (port > 0), then appends the server via ServerConfig::push_back. Centralizing the server block 
		logic here keeps parseTokens small and gives you a single place to add future server-scoped directives. 
		At runtime, each parsed VirtualServer becomes a bound listener with per-site policies 
		for static/CGI/proxy routing and uploads.
	
	*/

	static void parseServerBlock(ServerConfig &sc, const std::vector<std::string> &tok, std::size_t &i)
	{
		const std::size_t N = tok.size();
		if (i + 1 >= N || tok[i + 1] != "{")
			throw std::runtime_error("expected '{' after 'server'");
		i += 2; // skip "server" "{"

		VirtualServer vs;
		bool seen_listen = false;

		// inherit global upstream pools by default
		vs.upstreams = sc.upstream_pools;

		while (i < N && tok[i] != "}")
		{
			const std::string kw = tok[i++];

			if (kw == ";")
				continue; // tolerate stray semicolons

			if (kw == "listen")
			{
				parseListen(vs, tok, i);
				seen_listen = true;
				continue;
			}
			if (kw == "server_name")
			{
				parseServerName(vs, tok, i);
				continue;
			}
			if (kw == "root")
			{
				parseRoot(vs, tok, i);
				continue;
			}
			if (kw == "index")
			{
				parseIndex(vs, tok, i);
				continue;
			}
			if (kw == "error_page")
			{
				parseErrorPage(vs, tok, i);
				continue;
			}
			if (kw == "client_body_temp_path")
			{
				parseClientBodyTempPath(vs, tok, i);
				continue;
			}
			if (kw == "client_max_body_size")
			{
				parseClientMaxBodySize(vs, tok, i);
				continue;
			}

			if (kw == "upstream")
			{
				parsePerServerUpstream(vs, tok, i);
				continue;
			}
			if (kw == "location")
			{
				parseLocationBlock(vs, tok, i);
				continue;
			}

			throw std::runtime_error(std::string("unknown directive in server block: ") + kw);
		}

		if (i >= N || tok[i] != "}")
			throw std::runtime_error("missing '}' to close server block");
		if (!seen_listen || vs.listen_port <= 0)
			throw std::runtime_error("server missing valid listen directive");

		++i; // "}"
		sc.push_back(vs);
	}

} // namespace (helpers)

// =========================
// Public entry (refactored)
// =========================
void ServerConfig::parseTokens(const std::vector<std::string> &tok)
{
	_servers.clear();

	const std::size_t N = tok.size();
	std::size_t i = 0;

	while (i < N)
	{
		const std::string &t0 = tok[i];

		// GLOBAL
		if (t0 == "types")
		{
			parseTypesBlock(*this, tok, i);
			continue;
		}
		if (t0 == "cgi")
		{
			parseGlobalCgi(*this, tok, i);
			continue;
		}

		if (t0 == "session" ||
			t0 == "session_cookie_name" ||
			t0 == "session_max_age" ||
			t0 == "session_secure" ||
			t0 == "session_http_only" ||
			t0 == "session_same_site")
		{
			parseSessionDirective(*this, tok, i);
			continue;
		}

		if (t0 == "upstream")
		{
			parseGlobalUpstream(*this, tok, i);
			continue;
		}

		// SERVER
		if (t0 == "server")
		{
			parseServerBlock(*this, tok, i);
			continue;
		}

		// Global IP ACL
		if (t0 == "allowIp")
		{
			parseAllowIpBlock(*this, tok, i);
			continue;
		}
		if (t0 == "denyIp")
		{
			parseDenyIpBlock(*this, tok, i);
			continue;
		}
		if (t0 == "defaultAllowIp")
		{
			parseDefaultAllowIp(*this, tok, i);
			++i;
			continue;
		}

		// tolerate stray semicolons at top level
		if (t0 == ";")
		{
			++i;
			continue;
		}

		throw std::runtime_error(std::string("unknown top-level directive: ") + t0);
	}
}

/*

void ServerConfig::checkDuplicateListen_() const

A post-parse validator that prevents two server { … }
blocks from binding the same (host,port) pair.
It walks _servers, normalizes an empty host to 0.0.0.0,
and inserts (host, port) into a std::set; if insertion fails,
it throws a precise “duplicate listen” error. This avoids confusing
runtime failures (bind EADDRINUSE) and makes the configuration error
explicit to the user. Running it after parseTokens means all syntactic
and semantic details are already known; the check is a final sanity
step before Server::start() creates listeners and registers
AcceptorHandlers with the EventLoop. It keeps networking code
simple by ensuring preconditions are satisfied.


*/

void ServerConfig::checkDuplicateListen_() const
{
	std::set<std::pair<std::string, int> > seen;
	for (std::vector<VirtualServer>::size_type i = 0; i < _servers.size(); ++i)
	{
		const VirtualServer &vs = _servers[i];
		const int port = vs.listen_port;
		if (port <= 0 || port > 65535)
			continue;
		const std::string host = vs.listen_host.empty() ? std::string("0.0.0.0") : vs.listen_host;
		const std::pair<std::string, int> key(host, port);
		if (!seen.insert(key).second)
		{
			std::ostringstream oss;
			oss << "duplicate listen directive: " << host << ":" << port;
			throw std::runtime_error(oss.str());
		}
	}
}

/*

void ServerConfig::parseFile(const std::string& path)

A thin wrapper that wires file I/O to the parser:
it calls readWholeFile(path) to obtain the config text and then
delegates to parseString. Keeping this two-step split allows tests
to call parseString with in-memory text, while production code uses parseFile.
Any exceptions (open failures, parse errors) propagate to the caller for top-level
handling (e.g., logging “fatal: …” in main). After successful parsing,
Server can immediately consume the populated ServerConfig.

*/

void ServerConfig::parseFile(const std::string &path)
{
	const std::string data = readWholeFile(path);
	parseString(data);
}

/*

void ServerConfig::parseString(const std::string& text)

The in-memory entry point to the parser. It first tokenizes the string
with tokenize, clears _servers if the token set is empty (allowing “empty config”
semantics), otherwise calls parseTokens(tok) to build the config tree and finally
checkDuplicateListen_() to enforce unique listeners. This layering makes the parser
reusable and testable: unit tests can feed small snippets to validate each directive
or block without touching the filesystem. By ending with the duplicate-listen check,
it guarantees that a ServerConfig instance returned from parseString is ready for
Server::start() without further validation passes.

*/

void ServerConfig::parseString(const std::string &text)
{
	const std::vector<std::string> tok = tokenize(text);
	if (tok.empty())
	{
		_servers.clear();
		return;
	}
	parseTokens(tok);
	checkDuplicateListen_();
}
