/* --- CgiHandler.cpp --- */

/* ------------------------------------------
author: undefined
date: 8/10/2025
------------------------------------------ */

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
#include <sstream>
#include <vector>
#include <string>
#include <cstring>

// small helpers (already in your TU; keep one copy)
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

/* static std::string joinFs(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    const bool aSlash = a[a.size()-1] == '/';
    const bool bSlash = b[0] == '/';
    if (aSlash && bSlash) return a + b.substr(1);
    if (!aSlash && !bSlash) return a + "/" + b;
    return a + b;
} */

CgiHandler::CgiHandler() : Handler() {}
CgiHandler::~CgiHandler() {}

// keep your buildEnv implementation as-is (the tests exercise it)

bool CgiHandler::handle(HttpRequest &req, HttpResponse &res, RequestContext &ctx)
{
    // 0) Resolve CGI spec (per-location overrides global)
    CgiRegistry reg;
    const std::map<std::string, CgiSpec> *locMap = ctx.loc ? &ctx.loc->cgi_by_ext : 0;
    const std::map<std::string, CgiSpec> *defMap = ctx.cfg ? &ctx.cfg->cgi_defaults : 0;
    reg.setSources(locMap, defMap);

    const CgiSpec *spec = reg.findByExtension(ctx.cgi_ext);
    if (!spec)
        return false; // not handled here; let other handlers try

    // 1) Build the CGI environment
    std::vector<std::string> envv;
    buildEnv(req, *ctx.vs, envv); // returns >0 on success in your implementation

    // 2) Compute script filename (prefer location.root, else server root)
    const std::string &baseRoot = (ctx.loc && !ctx.loc->root.empty()) ? ctx.loc->root : ctx.vs->root;

    std::string root = baseRoot;
    if (!root.empty() && root[root.size() - 1] == '/')
        root.erase(root.size() - 1);

    std::string path = req.getPath(); // absolute, e.g. "/cgi-bin/hello.php"
    if (path.empty() || path[0] != '/')
        path = "/" + path;

    const std::string scriptPath = root + path; // e.g. "/var/www/html" + "/cgi-bin/hello.php"

    // 3) Spawn the CGI (argv = [bin, script], envp from envv)
    CgiProcess proc;
    if (!proc.spawn(*spec, scriptPath, envv))
    {
        // Minimal 500 response
        const char *body = "CGI spawn failed\n";
        const size_t blen = std::char_traits<char>::length(body);

        res.http_version = "HTTP/1.1";
        res.headers.set("Content-Type", "text/plain");
        std::ostringstream cl;
        cl << blen;
        res.headers.set("Content-Length", cl.str());
        res.body.assign(body, body + blen);
        res.bodyLength = res.body.size();
        return true;
    }

    // Not yet wired into the event loop: terminate to avoid leaking the child
    proc.terminate();

    // Placeholder response until async pipe wiring is implemented
    const char *body = "CGI spawned (placeholder). EventLoop wiring pending.\n";
    const size_t blen = std::char_traits<char>::length(body);

    res.http_version = "HTTP/1.1";
    res.headers.set("Content-Type", "text/plain");
    std::ostringstream cl;
    cl << blen;
    res.headers.set("Content-Length", cl.str());
    res.body.assign(body, body + blen);
    res.bodyLength = res.body.size();
    return true;
}
