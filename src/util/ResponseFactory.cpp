	/* --- ResponseFactory.cpp --- */

/* ------------------------------------------
author: undefined
date: 8/27/2025
------------------------------------------ */

#include "ResponseFactory.h"
#include "RequestContext.h"
#include "PathUtil.h"
#include "ETagUtil.h"
#include "HEADER_ENTRIES.h"
#include "VirtualServer.h"
#include "ServerConfig.h"
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <vector>
#include <ctime>
#include <sstream>
#include <map>

// --- tiny helpers ---

static const std::string& reasonFor(int code) 
{
    struct StatusPair { 
        int code;
        const char* reason; 
    };
    static const StatusPair status_array[] = {
        { 200,"OK" }, 
        { 204,"No Content" },
        { 301,"Moved Permanently" },
        { 302,"Found" }, 
        { 303,"See Other" },
        { 307,"Temporary Redirect" }, 
        { 308,"Permanent Redirect" },
        { 400,"Bad Request" },
        { 401,"Unauthorized" },
        { 403,"Forbidden" },
        { 404,"Not Found" },
        { 405,"Method Not Allowed" },
        { 411,"Length Required" },
        { 413,"Payload Too Large" },
        { 417,"Expectation Failed" },
        { 421,"Misdirected Request" },
        { 431,"Request Header Fields Too Large" },
        { 500,"Internal Server Error" },
        { 501,"Not Implemented" },
        { 502,"Bad Gateway" },
        { 503,"Service Unavailable" },
        { 505,"HTTP Version Not Supported" }
    };
    static std::map<int, std::string> k;
    if (k.empty()) {
        for (size_t i = 0; i < sizeof(status_array)/sizeof(status_array[0]); ++i)
            k[status_array[i].code] = status_array[i].reason;
    }
    static const std::string unk = "Unknown";
    std::map<int,std::string>::const_iterator it = k.find(code);
    return (it != k.end()) ? it->second : unk;
}

// RFC 7231 / RFC 9110 
static std::string rfc1123_now_gmt()
{
    char buf[64];
    std::time_t t = std::time(0);
    std::tm g;
#if defined(_WIN32)
    gmtime_s(&g, &t);
#else
    g = *std::gmtime(&t);
#endif
    std::strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", &g);
    return std::string(buf);
}

static void addCommonHeaders(HttpResponse &r, bool close)
{
    r.headers.set("Date", rfc1123_now_gmt());
    r.headers.set("Server", "webserv/1.0");
    r.headers.set("Connection", close ? "close" : "keep-alive");
}

// --- builders ---


HttpResponse ResponseFactory::makeError(int code,const std::string & reasons, bool close, const std::string& bodyText)
{
    HttpResponse r;
    r.status = code;
    r.reason = reasons.empty() ? reasonFor(code) : reasons;
    std::string text = bodyText.empty()
        ? (static_cast<std::ostringstream&>(std::ostringstream() << code << " " << r.reason << "\n")).str()
        : bodyText;
    r.body.assign(text.begin(), text.end());
    r.headers.set("Content-Type", "text/plain; charset=utf-8");
    std::ostringstream cl; cl << (unsigned long)r.body.size();
    r.headers.set("Content-Length", cl.str());
    r.bodyLength = r.body.size();
    addCommonHeaders(r, close);
    return r;
}


// makeText function is plain text fallback used in a few internal error sites.

HttpResponse ResponseFactory::makeText(int code,const std::string& text,const std::string & reasons,bool close)
{
    HttpResponse r;
    r.status = code;
    r.reason = reasons.empty() ? reasonFor(code) : reasons;
    r.body.assign(text.begin(), text.end());
    r.headers.set("Content-Type", "text/plain; charset=utf-8");
    std::ostringstream cl; cl << (unsigned long)r.body.size();
    r.headers.set("Content-Length", cl.str());
    r.bodyLength = r.body.size();
    addCommonHeaders(r, close);
    return r;
}

/* ---------- NEW: unified error page mapper (safe) ---------- */

static std::string httpDate(time_t t) {
    char buf[64];
    std::tm g = *std::gmtime(&t);
    std::strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", &g);
    return std::string(buf);
}

static std::string extLower(const std::string &p) {
    std::string::size_type dot = p.rfind('.');
    if (dot == std::string::npos)
        return "";
    std::string e = p.substr(dot + 1);
    for (size_t i=0; i<e.size(); ++i)
        if (e[i] >= 'A' && e[i] <= 'Z') e[i] = char(e[i] - 'A' + 'a');
    return e;
}

static std::string guessMime(const std::string &path,
                             const ServerConfig *cfg) {
    const std::string ext = extLower(path);
    if (cfg) {
        std::map<std::string,std::string>::const_iterator it = cfg->mime_mapping.find(ext);
        if (it != cfg->mime_mapping.end())
            return it->second;
    }
    if (ext=="html"||ext=="htm")
        return "text/html";
    if (ext=="css")
        return "text/css";
    if (ext=="js")
        return "application/javascript";
    if (ext=="json")
        return "application/json";
    if (ext=="png")
        return "image/png";
    if (ext=="jpg"||ext=="jpeg")
        return "image/jpeg";
    if (ext=="gif")
        return "image/gif";
    if (ext=="svg")
        return "image/svg+xml";
    if (ext=="txt")
        return "text/plain";
    return "application/octet-stream";
}



/* makeErrorOrPage(ctx, code, reason, close, extra) – 
picks a configured error page if present, otherwise falls back to a minimal text response. 
Sets Connection: close when you ask it to. */

HttpResponse ResponseFactory::makeErrorOrPage(const RequestContext &ctx,
                                              int code,
                                              const std::string &reason,
                                              bool close,
                                              const std::string &fallbackBody)
{
    HttpResponse r;
    r.setStatus(code, reason);
    addCommonHeaders(r, close);
    r.headers.set("Content-Type", "text/plain; charset=utf-8");

    // 1) Try to map configured error_page at server level
    std::string uri;
    if (ctx.vs) {
        std::map<int,std::string>::const_iterator it = ctx.vs->error_pages.find(code);
        if (it != ctx.vs->error_pages.end())
            uri = it->second;
    }
    // Sensible defaults if nothing configured
    if (uri.empty()) {
        if (code == 404)
            uri = "/404.html";
        else if (code == 500)
            uri = "/500.html";
    }

    // 2) If no mapping or no server, fallback to plain text body
    if (uri.empty() || !ctx.vs) {
        const std::string text = fallbackBody.empty()
            ? (static_cast<std::ostringstream&>(std::ostringstream() << code << " " << r.getReason() << "\n")).str()
            : fallbackBody;
        r.body.assign(text.begin(), text.end());
        std::ostringstream cl; cl << (unsigned long)r.body.size();
        r.headers.set("Content-Length", cl.str());
        r.bodyLength = r.body.size();
        return r;
    }

    // 3) Build base directory (effective_root > location.root > server.root)
    const std::string base = !ctx.effective_root.empty()
        ? ctx.effective_root
        : ((ctx.loc && !ctx.loc->root.empty()) ? ctx.loc->root : ctx.vs->root);

    std::string rel = uri;
    if (rel.empty() || rel[0] != '/')
        rel = "/" + rel;

    // 4) Canonicalize and ensure subpath of base (prevent traversal)
    std::string canonBase, canonErr;
    if (!PathUtil::canonicalize(base, canonBase) ||
        !PathUtil::canonicalize(base + rel, canonErr) ||
        canonErr.find(canonBase) != 0 ||
        (canonErr.size() > canonBase.size() && canonErr[canonBase.size()] != '/')) {
        const std::string text = fallbackBody.empty()
            ? (static_cast<std::ostringstream&>(std::ostringstream() << code << " " << r.getReason() << "\n")).str()
            : fallbackBody;
        r.body.assign(text.begin(), text.end());
        std::ostringstream cl; cl << (unsigned long)r.body.size();
        r.headers.set("Content-Length", cl.str());
        r.bodyLength = r.body.size();
        return r;
    }

    // 5) Read the mapped file
    struct stat st;
    if (::stat(canonErr.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
        const std::string text = fallbackBody.empty()
            ? (static_cast<std::ostringstream&>(std::ostringstream() << code << " " << r.getReason() << "\n")).str()
            : fallbackBody;
        r.body.assign(text.begin(), text.end());
        std::ostringstream cl; cl << (unsigned long)r.body.size();
        r.headers.set("Content-Length", cl.str());
        r.bodyLength = r.body.size();
        return r;
    }

    std::vector<char> buf;
    int fd = ::open(canonErr.c_str(), O_RDONLY);
    if (fd >= 0) {
        char tmp[8192]; ssize_t n;
        while ((n = ::read(fd, tmp, sizeof tmp)) > 0)
            buf.insert(buf.end(), tmp, tmp + n);
        ::close(fd);
    }

    r.body.assign(buf.begin(), buf.end());
    r.headers.set(HDR_CONTENT_TYPE, guessMime(canonErr, ctx.cfg));
    const std::string et = ETagUtil::generate(canonErr.c_str());
    if (!et.empty())
        r.headers.set(HDR_ETAG, et);
    const std::string lm = httpDate(st.st_mtime);
    if (!lm.empty())
        r.headers.set(HDR_LAST_MODIFIED, lm);
    std::ostringstream cl; cl << (unsigned long)r.body.size();
    r.headers.set("Content-Length", cl.str());
    r.bodyLength = r.body.size();
    return r;
}
