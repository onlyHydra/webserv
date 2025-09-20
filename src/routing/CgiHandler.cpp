/* --- CgiHandler.cpp --- */

/* ------------------------------------------
author: undefined
date: 8/10/2025
------------------------------------------ */
#include <stdio.h>   // for fprintf, stderr

#include "CgiHandler.h"
#include "CgiRegistry.h"
#include "CgiProcess.h"
#include "RequestContext.h"
#include "VirtualServer.h"
#include "ServerConfig.h"
#include "HttpRequest.h"
#include "HttpResponse.h"
#include "Headers.h"
#include "HEADER_ENTRIES.h"
#include "ClientHandler.h"
#include <sys/time.h>
#include <poll.h>
#include <sstream>
#include <vector>
#include <string>
#include <cstring>
#include <cerrno>
#include <cstdio>   // for fprintf, stderr

// small helpers (already in your TU; keep one copy)


/* 

#ifdef KEEP_HOST_HELPER static std::string hostWithoutPort(const std::string &hostHdr)
This helper normalizes the Host header into a canonical server name by removing any 
explicit port suffix while correctly supporting IPv6 bracket notation. 
For IPv6, it keeps the [addr] portion and discards a following :port; for IPv4/hostnames, 
it splits on the last colon. Normalization matters because CGI variables such as 
SERVER_NAME and SERVER_PORT are derived from Host, 
and inconsistent parsing produces incorrect environment values. By centralizing that logic, 
we avoid duplicating tricky edge-case handling across the codebase and ensure consistent 
behavior across platforms and requests.

 */

#ifdef KEEP_HOST_HELPER
static std::string hostWithoutPort(const std::string &hostHdr)
{
	if (hostHdr.empty())
		return hostHdr;
	if (hostHdr[0] == '[')
	{
		std::string::size_type rb = hostHdr.find(']');
		return (rb != std::string::npos) ? hostHdr.substr(0, rb + 1) : hostHdr;
	}
	std::string::size_type c = hostHdr.find(':');
	return (c == std::string::npos) ? hostHdr : hostHdr.substr(0, c);
}
#endif


/* 

int CgiHandler::buildEnv(const HttpRequest&, const VirtualServer&, std::vectorstd::string
&) const Builds a complete CGI environment array (envp) from HTTP request and server context.
It derives SERVER_NAME and SERVER_PORT from Host when provided (handling IPv6 brackets and explicit ports),
falls back to server names or listen host, and sets core variables like
GATEWAY_INTERFACE, REQUEST_METHOD, QUERY_STRING, SERVER_PROTOCOL, CONTENT_TYPE, and CONTENT_LENGTH
(including buffered body length if header missing). It computes DOCUMENT_ROOT and SCRIPT_FILENAME by
joining effective roots with the requested path, and exposes REMOTE_ADDR using X-Forwarded-For’s first token when present.
Finally, it appends REDIRECT_STATUS=200 for php-cgi compatibility. Returns the count.  

*/

int CgiHandler::buildEnv(const HttpRequest &req,
						const VirtualServer &vs,
						std::vector<std::string> &envv) const
{
	envv.clear();

	const Headers &H = req.getHeaders();

	// --- SERVER_NAME / SERVER_PORT (prefer Host header if present) ---
	std::string host = H.get(HDR_HOST);
	int server_port = vs.listen_port;
	if (!host.empty())
	{
		// strip optional port from Host
		if (host.size() && host[0] == '[')
		{
			// IPv6 in brackets: [::1]:8080
			std::string::size_type rb = host.find(']');
			if (rb != std::string::npos)
			{
				// try to parse ":PORT" after the closing bracket
				if (rb + 1 < host.size() && host[rb + 1] == ':')
				{
					const std::string pstr = host.substr(rb + 2);
					int p = 0;
					for (size_t i = 0; i < pstr.size(); ++i)
					{
						if (pstr[i] < '0' || pstr[i] > '9')
						{
							p = 0;
							break;
						}
						p = p * 10 + (pstr[i] - '0');
					}
					if (p > 0 && p <= 65535)
						server_port = p;
				}
				host = host.substr(0, rb + 1); // keep the [ipv6] part only
			}
		}
		else
		{
			// IPv4 / hostname form: host[:port]
			std::string::size_type c = host.find(':');
			if (c != std::string::npos)
			{
				const std::string pstr = host.substr(c + 1);
				int p = 0;
				bool ok = !pstr.empty();
				for (size_t i = 0; i < pstr.size(); ++i)
				{
					if (pstr[i] < '0' || pstr[i] > '9')
					{
						ok = false;
						break;
					}
					p = p * 10 + (pstr[i] - '0');
				}
				if (ok && p > 0 && p <= 65535)
					server_port = p;
				host = host.substr(0, c); // strip port from name
			}
		}
	}

	std::string server_name;
	if (!host.empty())
		server_name = host;
	else if (!vs.server_names.empty())
		server_name = vs.server_names[0];
	else if (!vs.listen_host.empty())
		server_name = vs.listen_host;
	else
		server_name = "localhost";

	std::ostringstream port_ss;
	port_ss << server_port;

	// --- CONTENT_* from headers/body ---
	std::string ctype = H.get(HDR_CONTENT_TYPE);
	std::string clen = H.get(HDR_CONTENT_LENGTH);
	if (clen.empty())
	{
		// if parser already buffered body, expose its length
		size_t blen = req.getBodyLength();
		if (blen > 0)
		{
			std::ostringstream cl;
			cl << blen;
			clen = cl.str();
		}
	}

	// --- REMOTE_ADDR: prefer X-Forwarded-For first token if present ---
	std::string remote = H.get(HDR_X_FORWARDED_FOR);
	if (!remote.empty())
	{
		std::string::size_type comma = remote.find(',');
		if (comma != std::string::npos)
			remote = remote.substr(0, comma);
		// trim spaces
		while (!remote.empty() && (remote[0] == ' ' || remote[0] == '\t'))
			remote.erase(0, 1);
		while (!remote.empty() && (remote[remote.size() - 1] == ' ' || remote[remote.size() - 1] == '\t'))
			remote.erase(remote.size() - 1);
	}

	// --- SCRIPT_NAME / DOCUMENT_ROOT / SCRIPT_FILENAME ---
	const std::string script_name = req.getPath(); // already a path like "/cgi/foo.php"

	std::string docroot = vs.root;
	if (!docroot.empty() && docroot[docroot.size() - 1] == '/')
		docroot.erase(docroot.size() - 1); // avoid double slashes

	// join docroot + script_name
	std::string script_filename;
	if (docroot.empty())
		script_filename = script_name;
	else if (!script_name.empty() && script_name[0] == '/')
		script_filename = docroot + script_name;
	else
		script_filename = docroot + "/" + script_name;

	// --- Required / common CGI variables ---
	envv.push_back("GATEWAY_INTERFACE=CGI/1.1");
	envv.push_back(std::string("REQUEST_METHOD=") + req.getMethod());
	envv.push_back(std::string("SCRIPT_NAME=") + script_name);
	envv.push_back(std::string("QUERY_STRING=") + req.getQuery());
	envv.push_back(std::string("SERVER_PROTOCOL=") + req.getHttpVer());
	envv.push_back(std::string("SERVER_NAME=") + server_name);
	envv.push_back(std::string("SERVER_PORT=") + port_ss.str());
	if (!ctype.empty())
		envv.push_back(std::string("CONTENT_TYPE=") + ctype);
	if (!clen.empty())
		envv.push_back(std::string("CONTENT_LENGTH=") + clen);
	envv.push_back(std::string("REMOTE_ADDR=") + remote);
	envv.push_back(std::string("DOCUMENT_ROOT=") + docroot);
	envv.push_back(std::string("SCRIPT_FILENAME=") + script_filename);

	// For php-cgi compatibility; harmless for others.
	envv.push_back("REDIRECT_STATUS=200");

	return static_cast<int>(envv.size()); // tests expect >0
}


/* 

CgiHandler::CgiHandler()
A lightweight constructor that keeps the handler effectively stateless and cheap to instantiate.
CgiHandler instances serve as plug-in components the pipeline can invoke after routing decides a request targets a CGI endpoint
(based on extension and location policy). Because the heavy lifting (mapping the extension, building the environment,
spawning the process, streaming I/O, enforcing deadlines) is delegated to CgiRegistry, CgiProcess, and CGIStreamer,
the constructor performs no allocations or side effects. This design keeps object lifetimes simple,
supports reuse across connections, and avoids per-request overhead, fitting cleanly into the non-blocking, single-poll event-driven architecture.


*/

CgiHandler::CgiHandler() : Handler() {}

/* 

CgiHandler::~CgiHandler()
The destructor is intentionally trivial because CgiHandler holds no owning resources; process management,
pipes, and buffers are owned by CGIStreamer/CgiProcess or managed by the pipeline/loop.
Keeping destruction cheap matters in a server that frequently constructs handlers or stores them behind
base pointers; it ensures polymorphic deletion through Handler* remains safe and doesn’t introduce hidden
synchronization, syscalls, or buffering flushes. In short, the destructor’s “do nothing” contract confirms
RAII responsibilities stay with the components that actually allocate operating-system resources, reducing
complexity and preventing accidental double-closes or lifetime confusion during shutdown paths.

*/

CgiHandler::~CgiHandler() {}
// keep your buildEnv implementation as-is (the tests exercise it)





/* 


bool CgiHandler::handle(HttpRequest&, HttpResponse&, RequestContext&)
Orchestrates a CGI request. It resolves the applicable CgiSpec by merging per-location overrides
with server defaults; if none matches the extension, it returns false so other handlers can try.
Next, it builds the environment, determines the absolute filesystem script path from the effective root
plus full URL path (not stripping /cgi/...), and verifies readability. It then asks ctx.cgi_streamer to beginCgi,
logging pipe FDs. If the request has no body, it proactively closes CGI stdin to unblock the script.
It returns false to indicate asynchronous processing; the EventLoop and ClientHandler continue streaming output.


*/

bool CgiHandler::handle(HttpRequest &req, HttpResponse &res, RequestContext &ctx)
{

	std::fprintf(stderr, "[CGI] handle enter method=%s path=%s body_len=%zu temp=%d\n",
			req.getMethod().c_str(), req.getPath().c_str(),
			(size_t)req.getBodyLength(), (int)ctx.temp_file_used);

	// 0) Resolve CGI spec (per-location overrides global)
	CgiRegistry reg;
	const std::map<std::string, CgiSpec>* locMap = ctx.loc ? &ctx.loc->cgi_by_ext : 0;
	const std::map<std::string, CgiSpec>* defMap = ctx.cfg ? &ctx.cfg->cgi_defaults : 0;
	reg.setSources(locMap, defMap);

	const CgiSpec* spec = reg.findByExtension(ctx.cgi_ext);
	if (!spec)
		return false; // not a CGI target; let other handlers try
	if (!ctx.cgi_streamer) {
		res = ResponseFactory::makeText(500, "CGI unavailable\n", "Internal Server Error", true);
		return true;
	}
	// 1) Build the CGI environment
	std::vector<std::string> envv;
	if (buildEnv(req, *ctx.vs, envv) <= 0) {
		res = ResponseFactory::makeText(500, "CGI env build failed\n", "Internal Server Error", true);
		return true;
	}

	// 2) Compute script filename (prefer effective_root, else loc/server root)
	const std::string& baseRoot =
		!ctx.effective_root.empty() ? ctx.effective_root
		: (ctx.loc && !ctx.loc->root.empty() ? ctx.loc->root : ctx.vs->root);

	std::string root = baseRoot;
	if (!root.empty() && root[root.size() - 1] == '/') 
		root.erase(root.size() - 1);

	// IMPORTANT: use the FULL request path so "/cgi/..." is preserved
	std::string urlPath = req.getPath();                 // e.g. "/cgi/hello.py"
	if (urlPath.empty() || urlPath[0] != '/') 
		urlPath = "/" + urlPath;

	const std::string scriptPath = root + urlPath;       // "./www" + "/cgi/hello.py"

	// ---- DEBUG + EARLY GUARD ----
	{
		std::ostringstream dbg;
		dbg << "[CGI] url=" << req.getPath()
			<< " root=" << root
			<< " scriptPath=" << scriptPath;
		std::string line = dbg.str();
		::write(2, line.c_str(), line.size());
		::write(2, "\n", 1);

		if (::access(scriptPath.c_str(), R_OK) != 0) {
			int se = errno;
			std::ostringstream msg;
			msg << "CGI script not found at: " << scriptPath
				<< " (errno " << se << ": " << std::strerror(se) << ")\n";
			res = ResponseFactory::makeText(404, msg.str(), "Not Found", true);
			return true;
		}
	}
	// ------------------------------

	// 3) Start CGI asynchronously
	if (!ctx.cgi_streamer->beginCgi(*spec, scriptPath, envv)) {
		res = ResponseFactory::makeText(500, "CGI spawn failed\n", "Internal Server Error", true);
		return true;
	}

	// >>> ADD THIS RIGHT HERE: print the pipe FDs we will poll <<<
	{
		int in  = ctx.cgi_streamer->cgiStdinFD();
		int out = ctx.cgi_streamer->cgiStdoutFD();
		std::fprintf(stderr, "[CGI] begin ok inFD=%d outFD=%d\n", in, out);
	}
	// <<<----------------------------------------------------------------->>>

	// 4) If there is no request body to feed, close CGI stdin now so the script
	//    doesn't block waiting for EOF (typical for GET/HEAD).
	const bool has_body = (req.getBodyLength() > 0) || ctx.temp_file_used;

	if (has_body) {
		int inFD  = ctx.cgi_streamer->cgiStdinFD();
		int outFD = ctx.cgi_streamer->cgiStdoutFD();
		std::fprintf(stderr, "[CGI] register POLLOUT for inFD=%d; POLLIN for outFD=%d\n", inFD, outFD);
	} else if (req.getMethod() != "POST") {
		// For GET/HEAD with no body, close stdin so the CGI won’t hang waiting for EOF.
		ctx.cgi_streamer->closeStdin();
	}
	// Async: EventLoop/ClientHandler will pump CGI pipes and stream output.
	return false;
}




