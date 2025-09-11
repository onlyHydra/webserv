/* --- StaticHandler.cpp --- */

/* ------------------------------------------
author: undefined
date: 8/10/2025
------------------------------------------ */

/* --- StaticHandler.cpp (implemented) --- */
/* --- StaticHandler.cpp --- */

#include "StaticHandler.h"
#include "RequestContext.h"
#include "VirtualServer.h"
#include "ServerConfig.h"
#include "HttpRequest.h"
#include "HttpResponse.h"
#include "Headers.h"
#include "HEADER_ENTRIES.h"
#include "HttpPreconditions.h"


#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <cstdio>
#include <sstream>
#include <vector>
#include <string>
#include <algorithm>
#include <map>
#include <limits.h> // PATH_MAX

// ---------------- small helpers ----------------

static std::string toLower(const std::string &s)
{
    std::string t = s;
    for (size_t i = 0; i < t.size(); ++i)
    {
        unsigned char c = static_cast<unsigned char>(t[i]);
        if (c >= 'A' && c <= 'Z')
            t[i] = char(c - 'A' + 'a');
    }
    return t;
}

static std::string extOf(const std::string &p)
{
    std::string::size_type dot = p.rfind('.');
    if (dot == std::string::npos)
        return "";
    return toLower(p.substr(dot + 1));
}


// RFC 7231 IMF-fixdate (e.g., "Wed, 21 Oct 2015 07:28:00 GMT")
static std::string httpDate(time_t t)
{
    char buf[64];
    struct tm g = *::gmtime(&t);
    if (std::strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", &g))
        return std::string(buf);
    return std::string();
}



static std::string guessMime(const std::string &path, const ServerConfig *cfg)
{
    const std::string ext = extOf(path);
    if (cfg)
    {
        std::map<std::string, std::string>::const_iterator it = cfg->mime_mapping.find(ext);
        if (it != cfg->mime_mapping.end())
            return it->second;
    }
    if (ext == "html" || ext == "htm")
        return "text/html";
    if (ext == "css")
        return "text/css";
    if (ext == "js")
        return "application/javascript";
    if (ext == "json")
        return "application/json";
    if (ext == "png")
        return "image/png";
    if (ext == "jpg" || ext == "jpeg")
        return "image/jpeg";
    if (ext == "gif")
        return "image/gif";
    if (ext == "svg")
        return "image/svg+xml";
    if (ext == "txt")
        return "text/plain";
    return "application/octet-stream";
}

static bool readWholeFile(const std::string &path, std::vector<char> &out)
{
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0)
        return false;
    char buf[8192];
    ssize_t n;
    while ((n = ::read(fd, buf, sizeof(buf))) > 0)
        out.insert(out.end(), buf, buf + n);
    int saved = errno;
    ::close(fd);
    return (n >= 0) || (saved == 0);
}

static bool realpathString(const std::string &in, std::string &out)
{
    char tmp[PATH_MAX];
    if (::realpath(in.c_str(), tmp) == 0)
        return false;
    out.assign(tmp);
    return true;
}

static bool isSubPath(const std::string &base, const std::string &p)
{
    if (base.empty()) return false;
    if (p.size() < base.size()) return false;
    if (p.compare(0, base.size(), base) != 0) return false;
    if (p.size() == base.size()) return true;
    return p[base.size()] == '/';
}

static std::string htmlEscape(const std::string &s)
{
    std::string o; o.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i)
    {
        switch (s[i])
        {
        case '&': o += "&amp;";  break;
        case '<': o += "&lt;";   break;
        case '>': o += "&gt;";   break;
        case '"': o += "&quot;"; break;
        case '\'': o += "&#39;"; break;
        default: o.push_back(s[i]); break;
        }
    }
    return o;
}

static std::string joinUrl(const std::string &a, const std::string &b)
{
    if (a.empty()) return b;
    if (b.empty()) return a;
    bool as = a[a.size() - 1] == '/';
    bool bs = b[0] == '/';
    if (as && bs)   return a + b.substr(1);
    if (!as && !bs) return a + "/" + b;
    return a + b;
}

static std::string buildAutoindex(const std::string &urlBase, const std::string &fsPath)
{
    DIR *d = ::opendir(fsPath.c_str());
    if (!d) return "";
    std::vector<std::string> entries;
    struct dirent *de;
    while ((de = ::readdir(d)) != 0)
    {
        const char *name = de->d_name;
        if (!::strcmp(name, ".") || !::strcmp(name, "..")) continue;
        entries.push_back(name);
    }
    ::closedir(d);
    std::sort(entries.begin(), entries.end());
    std::ostringstream html;
    html << "<!doctype html><html><head><meta charset=\"utf-8\">"
         << "<title>Index of " << htmlEscape(urlBase) << "</title></head><body>"
         << "<h1>Index of " << htmlEscape(urlBase) << "</h1><ul>";
    for (size_t i = 0; i < entries.size(); ++i)
    {
        const std::string &e = entries[i];
        html << "<li><a href=\"" << htmlEscape(joinUrl(urlBase, e)) << "\">"
             << htmlEscape(e) << "</a></li>";
    }
    html << "</ul></body></html>";
    return html.str();
}

// DISCONTINUED, UNUSED
// static std::string makeEtag(const struct stat &st)
// {
//     std::ostringstream et;
//     et << "\"" << std::hex << (unsigned long long)st.st_size
//        << "-" << std::hex << (unsigned long long)st.st_mtime << "\"";
//     return et.str();
// }

// Try to serve a configured error page, e.g. error_page 404 /errors/404.html;
// Falls back to /errors/404.html under the effective root if not configured.
// NOTE: We do not change status-line (serializer sends 200). This only swaps the body.
// Try to serve a configured error page at server level (VirtualServer).
// Falls back to /errors/404.html (or 500) under the effective root.
// NOTE: We do not change the status line (serializer still emits 200).
static bool serveErrorPage_(int code,
                            const RequestContext& ctx,
                            HttpResponse& res,
                            bool is_head)
{
    // Always set the status line for the error
    res.setStatus(code);

    // 1) Resolve mapped URI from the *server* (VirtualServer) only
    std::string uri;
    if (ctx.vs) {
        std::map<int,std::string>::const_iterator it = ctx.vs->error_pages.find(code);
        if (it != ctx.vs->error_pages.end())
            uri = it->second;
    }
    if (uri.empty()) {
        // Sensible default if nothing configured
        if (code == 404) uri = "/errors/404.html";
        else if (code == 500) uri = "/errors/500.html";
        else uri = "/errors/404.html";
    }

    // 2) Build filesystem path: effective_root (or loc root, else vs root) + uri
    const std::string base = !ctx.effective_root.empty()
        ? ctx.effective_root
        : ((ctx.loc && !ctx.loc->root.empty()) ? ctx.loc->root : ctx.vs->root);

    std::string rel = uri;
    if (rel.empty() || rel[0] != '/') rel = "/" + rel;

    std::string fs = base;
    if (!fs.empty() && fs[fs.size()-1] == '/') fs.erase(fs.size()-1);
    fs += rel;

    // 3) Canonicalize and safety check
    std::string canonBase, canonErr;
    if (!realpathString(base, canonBase) ||
        !realpathString(fs, canonErr) ||
        !isSubPath(canonBase, canonErr))
    {
        // Fallback: empty body, plain text
        res.body.clear();
        res.headers.set(HDR_CONTENT_TYPE, "text/plain");
        res.headers.set(HDR_CONTENT_LENGTH, "0");
        res.bodyLength = 0;
        return true;
    }

    // 4) Read and emit the error file
    struct stat st;
    if (::stat(canonErr.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
        res.body.clear();
        res.headers.set(HDR_CONTENT_TYPE, "text/plain");
        res.headers.set(HDR_CONTENT_LENGTH, "0");
        res.bodyLength = 0;
        return true;
    }

    std::vector<char> file;
    if (!readWholeFile(canonErr, file)) {
        res.body.clear();
        res.headers.set(HDR_CONTENT_TYPE, "text/plain");
        res.headers.set(HDR_CONTENT_LENGTH, "0");
        res.bodyLength = 0;
        return true;
    }

    res.body.clear();
    if (!is_head)
        res.body.assign(file.begin(), file.end());

    res.headers.set(HDR_CONTENT_TYPE, guessMime(canonErr, ctx.cfg));
    res.headers.set(HDR_ETAG, ETagUtil::generate(canonErr.c_str()));

    std::ostringstream cl;
    cl << static_cast<unsigned long>(file.size());
    res.headers.set(HDR_CONTENT_LENGTH, cl.str());
    res.bodyLength = file.size();
    return true;
}



// ---------------- constructors (linker needed these) ----------------
StaticHandler::StaticHandler() {}
StaticHandler::~StaticHandler() {}

// ---------------- main handler --------------------------------------
bool StaticHandler::handle(HttpRequest &req, HttpResponse &res, RequestContext &ctx)
{
    // Only GET/HEAD; soft-fail others with empty body (serializer always sends 200).
    const std::string m = req.getMethod();
    const bool is_head = (m == "HEAD");
    if (m != "GET" && !is_head) {
        res.body.clear();
        res.headers.set(HDR_CONTENT_TYPE, "text/plain");
        res.headers.set(HDR_CONTENT_LENGTH, "0");
        res.bodyLength = 0;
        return true;
    }

    // Prefer router/pipeline-computed paths
    const std::string base = !ctx.effective_root.empty()
        ? ctx.effective_root
        : ((ctx.loc && !ctx.loc->root.empty()) ? ctx.loc->root : ctx.vs->root);

    std::string rel = !ctx.rel_path.empty() ? ctx.rel_path : req.getPath();
    if (rel.empty() || rel[0] != '/') rel = "/" + rel;

    std::string fsCandidate = base;
    if (!fsCandidate.empty() && fsCandidate[fsCandidate.size() - 1] == '/')
        fsCandidate.erase(fsCandidate.size() - 1);
    fsCandidate += rel;

#if defined(DEBUG) || defined(UNIT_TEST)
    LOG_INFO("effective_root=" << base << " rel_path=" << rel << " fs=" << fsCandidate);
#endif

    // Canonicalize and block traversal
    std::string canonRoot, canonPath;
    if (!realpathString(base, canonRoot) ||
        !realpathString(fsCandidate, canonPath) ||
        !isSubPath(canonRoot, canonPath))
    {
        // Illegal traversal or bad root — show 404 page if available
        return serveErrorPage_(404, ctx, res, is_head);
    }

    struct stat st;
    if (::stat(canonPath.c_str(), &st) != 0) {
        // Not found -> try error page
        return serveErrorPage_(404, ctx, res, is_head);
    }

    if (S_ISDIR(st.st_mode)) {
        // Try index files: location first, then server
        std::vector<std::string> idx;
        if (ctx.loc) idx.insert(idx.end(), ctx.loc->index_files.begin(), ctx.loc->index_files.end());
        if (idx.empty() && ctx.vs) idx = ctx.vs->index_files;

        for (size_t i = 0; i < idx.size(); ++i) {
            std::string candidate = canonPath;
            if (candidate.empty() || candidate[candidate.size() - 1] != '/')
                candidate += "/";
            candidate += idx[i];

            struct stat st2;
            if (::stat(candidate.c_str(), &st2) == 0 && S_ISREG(st2.st_mode)) {
                std::vector<char> file;
                (void)readWholeFile(candidate, file);

                res.body.clear();
                if (!is_head) res.body.assign(file.begin(), file.end());

                res.headers.set(HDR_CONTENT_TYPE, guessMime(candidate, ctx.cfg));
                res.headers.set(HDR_ETAG, ETagUtil::generate(candidate.c_str()));

                std::ostringstream cl; cl << (unsigned long)file.size();
                res.headers.set(HDR_CONTENT_LENGTH, cl.str());
                res.bodyLength = file.size();
                return true;
            }
        }

        // Autoindex if enabled
        const bool autoindex = (ctx.loc ? ctx.loc->autoindex : false);
        if (autoindex) {
            std::string urlBase = rel;
            if (urlBase.empty() || urlBase[urlBase.size() - 1] != '/')
                urlBase += "/";
            const std::string html = buildAutoindex(urlBase, canonPath);

            res.body.clear();
            if (!is_head) res.body.assign(html.begin(), html.end());

            res.headers.set(HDR_CONTENT_TYPE, "text/html; charset=utf-8");
            std::ostringstream cl; cl << (unsigned long)html.size();
            res.headers.set(HDR_CONTENT_LENGTH, cl.str());
            res.bodyLength = html.size();
            return true;
        }

        // Directory without index and autoindex off -> 404 page if available
        return serveErrorPage_(404, ctx, res, is_head);
    }

    if (S_ISREG(st.st_mode)) {
    // Build ETag and Last-Modified first (we’ll need them for 304)
    const std::string et = ETagUtil::generate(canonPath.c_str());
    const std::string lm = httpDate(st.st_mtime);

    // Conditional GET handling
    if (!HttpPreconditions::getPreconditons(req, et, st.st_mtime))
    {
        res.setStatus(304);
        res.body.clear();
        res.headers.set(HDR_ETAG, et);
        if (!lm.empty()) res.headers.set(HDR_LAST_MODIFIED, lm);
        res.headers.set(HDR_CONTENT_LENGTH, "0");
        res.bodyLength = 0;
        return true;
    }

    // Normal 200 body
    std::vector<char> file;
    if (!readWholeFile(canonPath, file)) {
        // Failed to read: 404 page fallback (or empty)
        return serveErrorPage_(404, ctx, res, is_head);
    }

    res.body.clear();
    if (!is_head) res.body.assign(file.begin(), file.end());

    res.headers.set(HDR_CONTENT_TYPE, guessMime(canonPath, ctx.cfg));
    res.headers.set(HDR_ETAG, et);
    if (!lm.empty()) res.headers.set(HDR_LAST_MODIFIED, lm);

    std::ostringstream cl; cl << (unsigned long)file.size();
    res.headers.set(HDR_CONTENT_LENGTH, cl.str());
    res.bodyLength = file.size();
    return true;
}


    // Not a dir or regular file -> 404 page if available
    return serveErrorPage_(404, ctx, res, is_head);
}
