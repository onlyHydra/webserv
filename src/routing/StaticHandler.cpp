/* --- StaticHandler.cpp --- */

/* ------------------------------------------
author: undefined
date: 8/10/2025
------------------------------------------ */

#include "StaticHandler.h"

// ---------------- small helpers ----------------


/* 

static std::string toLower(const std::string& s)

Iterates characters, converting ASCII A–Z to lowercase in-place, producing a lowercase copy. 
This normalization supports case-insensitive handling where appropriate, 
like extension lookups and MIME guesses. Using a tiny loop avoids locale pitfalls and extra 
allocations from heavier utilities. Centralizing the behavior prevents scattered, inconsistent lowercase 
conversions that could diverge under edge cases. Although small, it’s performance-sensitive: it may run for many 
requests when computing content types or handling directory listings. 
Keeping it local to the translation unit makes intent obvious and linkage private, 
simplifying reasoning about code paths that depend on lowercase comparisons.

*/

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

/* 

static std::string extOf(const std::string& p)

Returns the lowercase file extension (without the dot) by searching the last dot in a path. 
If there’s no dot, returns empty. It deliberately ignores dots in directory components by using the last occurrence, 
matching typical filesystem semantics. Providing extensions normalized enables simple dictionary lookups for MIME mapping,
avoiding repeated parsing across the handler. By returning empty for “no extension,” the subsequent guessMime can gracefully 
fallback to application/octet-stream or configuration defaults. This keeps the content-type 
logic robust across static assets, directories, and resources that don’t carry extensions, like generated or hidden files.


*/


static std::string extOf(const std::string &p)
{
	std::string::size_type dot = p.rfind('.');
	if (dot == std::string::npos)
		return "";
	return toLower(p.substr(dot + 1));
}


// RFC 7231 IMF-fixdate (e.g., "Wed, 21 Oct 2015 07:28:00 GMT")


/* 

Formats a time_t as an IMF-fixdate (RFC 7231) using gmtime and strftime, 
yielding strings like “Wed, 21 Oct 2015 07:28:00 GMT.” Although StaticHandler mainly 
uses conditional logic for 304 handling, having a helper that produces standardized dates 
improves correctness when setting Last-Modified. A accurate, standardized date string ensures 
compatibility with browsers, proxies, and validators, avoiding cache inconsistencies. 
Putting it here avoids duplicated date formatting code elsewhere. If formatting fails (very unlikely), 
it returns an empty string, and callers simply skip the header rather than emitting malformed timestamps.

*/


static std::string httpDate(time_t t)
{
	char buf[64];
	struct tm g = *::gmtime(&t);
	if (std::strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", &g))
		return std::string(buf);
	return std::string();
}

/* 

Determines a MIME type. First, it checks server configuration’s mime_mapping using the lowercase extension. 
If not found, it falls back to common types (text/html, application/javascript, image, etc.), 
defaulting to application/octet-stream. Centralized guessing ensures consistent Content-Type headers 
for static files regardless of where they’re served (index page, directory file, or error page). 
Relying on configuration first enables user-controlled mappings and future extensibility 
(e.g., adding webp, avif). Returning a string rather than enum keeps serialization trivial 
and avoids lookup overhead during response header generation. 
This balance of config and curated defaults yields practical coverage.

*/

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

/* 

Opens the path read-only, then repeatedly reads into a stack buffer, appending to out. 
Returns false on open failure; otherwise returns whether the final read didn’t fail after close. 
Using a loop with 8KB chunks balances throughput and memory, avoiding large allocations for big files. 
The handler then copies these bytes into the HTTP response body when appropriate. By isolating the I/O, 
the rest of the handler remains focused on HTTP semantics. This function purposely avoids mmap 
for portability and to keep control over error handling and partial reads. 


*/

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

/* 

static bool realpathString(const std::string& in, std::string& out)

Calls realpath to canonicalize a filesystem path into an absolute, 
symlink-resolved string. Returns false when resolution fails. 
Canonicalization is vital before serving content: it allows the handler to enforce that the resolved target 
remains inside the configured root, protecting against directory traversal attacks. Placing this in a helper 
simplifies repeated use for both base root and candidate paths. Using real filesystem resolution 
(not purely string manipulation) ensures defenses remain effective even when symlinks or .. 
sequences could otherwise escape the document root unintentionally

*/

static bool realpathString(const std::string &in, std::string &out)
{
	char tmp[PATH_MAX];
	if (::realpath(in.c_str(), tmp) == 0)
		return false;
	out.assign(tmp);
	return true;
}


/* 

static bool isSubPath(const std::string& base, const std::string& p)

Verifies that canonicalized path p lies within canonicalized base. 
It checks prefix equality and, if longer, requires a separating slash at the boundary 
to prevent false positives (e.g., /wwwroot2 vs /wwwroot). 
Enforcing this ensures resources cannot escape the configured document root even through 
clever path manipulations or symlinks. The function is used immediately after realpathString 
to make a definitive authorization decision about serving content. 
Its simple, allocation-free checks make it appropriate for the hot path on every static file access.


*/

static bool isSubPath(const std::string &base, const std::string &p)
{
	if (base.empty())
		return false;
	if (p.size() < base.size())
		return false;
	if (p.compare(0, base.size(), base) != 0)
		return false;
	if (p.size() == base.size())
		return true;
	return p[base.size()] == '/';
}


/* 

static std::string htmlEscape(const std::string& s)


Escapes &, <, >, ", and ' to their HTML entities. 
It’s used when generating autoindex directory listings so filenames render 
safely and cannot inject markup or scripts. The function builds the output incrementally, 
reserving capacity for efficiency. Centralizing escaping prevents subtle inconsistencies 
and makes security review straightforward. Without correct escaping, a crafted filename could 
produce XSS in directory listings, especially important when serving public directories or 
uploads where filenames are user-controlled. This helper keeps the autoindex HTML simple, 
secure, and standards-compliant

*/

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


/* 

static std::string joinUrl(const std::string& a, const std::string& b)

Concatenates two URL path components with exactly one / separator, handling the four cases 
(a ends with /, b starts with /, both, neither). Avoiding accidental double or missing slashes ensures clean 
links inside the autoindex page and reduces redirect noise when users click directory children. 
Keeping URL joins predictable also improves cacheability and relative navigation stability across browsers. 
By handling only path concatenation (not escaping), the function stays focused and correct for server-generated directory listings.


*/

static std::string joinUrl(const std::string &a, const std::string &b)
{
	if (a.empty())
		return b;
	if (b.empty())
		return a;
	bool as = a[a.size() - 1] == '/';
	bool bs = b[0] == '/';
	if (as && bs)
		return a + b.substr(1);
	if (!as && !bs)
		return a + "/" + b;
	return a + b;
}


/* 

static std::string buildAutoindex(const std::string& urlBase, const std::string& fsPath)

Opens the directory, collects entries excluding . and .., sorts them, 
and emits a minimal HTML page listing each item as a hyperlink relative to urlBase. 
All names are escaped with htmlEscape, and links are joined via joinUrl. 
This provides a user-friendly directory view when autoindex is enabled and no index file exists. 
It’s intentionally simple, dependency-free HTML that browsers can render quickly. 
Generating it on the fly avoids storing templates and ensures correctness with current directory contents.

*/

static std::string buildAutoindex(const std::string &urlBase, const std::string &fsPath)
{
	DIR *d = ::opendir(fsPath.c_str());
	if (!d)
		return "";
	std::vector<std::string> entries;
	struct dirent *de;
	while ((de = ::readdir(d)) != 0)
	{
		const char *name = de->d_name;
		if (!::strcmp(name, ".") || !::strcmp(name, ".."))
			continue;
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

/**
 * @brief Prepares barebones Reponse
 * 
 * Sets the status, clears body and sets Content-Type and Content-Length
 * as well as setting the bodyLength field to 0
 * @param res Response to prepate
 * @param status Status code to set Response to
 * @param state Return value of this function
 */
static bool prepareResponse(HttpResponse &res, bool state)
{
	res.body.clear();
	res.headers.set(HDR_CONTENT_TYPE, "text/plain");
	res.headers.set(HDR_CONTENT_LENGTH, "0");
	res.bodyLength = 0;
	return (state);
}

/**
 * @brief Overload of function to set Response status
 */
static bool prepareResponse(HttpResponse &res, bool state, int status)
{
    res.setStatus(status);
	return (prepareResponse(res, state));
}

/**
 * @brief Prepares a fallback Response
 * @param res Response to set
 * 
 * In the case, that the Handler wants to give back an Error Page, 
 * but the file for that page is missing, this should be called
 */
static bool fallback404(HttpResponse &res)
{
	res.clearBody();
	res.setBody(FALLBACK_404);
	return (true);
}

/**
 * @brief Prepare Response to be based off of an Error file
 * @param code Http Code that specifies the Error file to look for (404 -> /errors/404.html)
 * @param ctx Context, for finding the Error files
 * @param res Response to be set
 * @param is_head Set by the HEAD method (Legacy)
 * 
 * Sets the given code as Response code
 * Then searches if it can find the Error Page in the Virtual Servers Pages
 * If not, then it uses some fallback file names
 * Then builds the filePath for that Error File
 * If the Error File cannot be accessed or read, uses a fallback response (404)
 * If the file could be found, use it as Response body
 * Then prepare rest of Response
 */
static bool serveErrorPage(int code,
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
        // If no specific error page found for the given error code, use theese fallbacks
        if (code == 404) uri = "/errors/404.html";
		else if (code == 403) uri = "/errors/403.html";
        else if (code == 500) uri = "/errors/500.html";
        else uri = "/errors/404.html";
    }

    // 2) Build filesystem path: effective_root (or loc root, else vs root) + uri
	std::string base;
	// Try to use Virtual Servers root as root for errors
	if (ctx.vs && !ctx.vs->root.empty())
		base = ctx.vs->root;
	else
	{
    	// Fallback, make current path as root (/home/whatever/www)
		char *c_path = getcwd(NULL, 0);
		base = "/";
		if (c_path)
		{
			base = std::string(c_path) + std::string("/www");
			free(c_path);
		}
	}

    // 3) Canonicalize and safety check
    std::string path = base + uri;
    std::string canonBase, canonErr;
    if (!realpathString(base, canonBase) ||
        !realpathString(path, canonErr) ||
        !isSubPath(canonBase, canonErr))
		return (fallback404(res));

    // 4) Read and emit the error file
    struct stat st;
    if (::stat(canonErr.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
		return (fallback404(res));
	
    std::vector<char> file;
    if (!readWholeFile(canonErr, file))
		return (fallback404(res));

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

bool StaticHandler::handleGet(const std::string &canonPath, const std::string &rel, \
	bool is_head, HttpRequest &req, HttpResponse &res, RequestContext &ctx)
{
	struct stat st;
    if (::stat(canonPath.c_str(), &st) != 0) {
        // Not found -> try error page
        return serveErrorPage(HTTP_NOT_FOUND, ctx, res, is_head);
    }

	if (S_ISDIR(st.st_mode))
	{
        // Try index files: location first, then server
        std::vector<std::string> idx;
        if (ctx.loc)
			idx.insert(idx.end(), ctx.loc->index_files.begin(), ctx.loc->index_files.end());
        if (idx.empty() && ctx.vs)
			idx = ctx.vs->index_files;

        for (size_t i = 0; i < idx.size(); ++i)
		{
            std::string candidate = canonPath;
            if (candidate.empty() || candidate[candidate.size() - 1] != '/')
                candidate += "/";
            candidate += idx[i];

            struct stat st2;
            if (::stat(candidate.c_str(), &st2) == 0 && S_ISREG(st2.st_mode))
			{
                std::vector<char> file;
                (void)readWholeFile(candidate, file);

				res.body.clear();
				if (!is_head)
					res.body.assign(file.begin(), file.end());

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
			if (!is_head)
				res.body.assign(html.begin(), html.end());

			res.headers.set(HDR_CONTENT_TYPE, "text/html; charset=utf-8");
			std::ostringstream cl; cl << (unsigned long)html.size();
			res.headers.set(HDR_CONTENT_LENGTH, cl.str());
			res.bodyLength = html.size();
			return true;
		}

        // Directory without index and autoindex off -> 404 page if available
        return serveErrorPage(HTTP_NOT_FOUND, ctx, res, is_head);
    }

    if (S_ISREG(st.st_mode))
	{
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
		if (!readWholeFile(canonPath, file))
		{
			// Failed to read: 404 page fallback (or empty)
			return serveErrorPage(HTTP_NOT_FOUND, ctx, res, is_head);
		}

		res.body.clear();
		if (!is_head)
			res.body.assign(file.begin(), file.end());
		res.headers.set(HDR_CONTENT_TYPE, guessMime(canonPath, ctx.cfg));
		res.headers.set(HDR_ETAG, et);
		if (!lm.empty())
			res.headers.set(HDR_LAST_MODIFIED, lm);
		std::ostringstream cl; cl << (unsigned long)file.size();
		res.headers.set(HDR_CONTENT_LENGTH, cl.str());
		res.bodyLength = file.size();
		return true;
	}
	// Not a dir or regular file -> 404 page if available
    return serveErrorPage(HTTP_NOT_FOUND, ctx, res, is_head);
}

bool StaticHandler::handleDelete(const std::string &path, HttpResponse res, RequestContext ctx)
{
	struct stat st;

	// Check if target exists
	if (::stat(path.c_str(), &st) != 0)
		return (serveErrorPage(HTTP_NOT_FOUND, ctx, res, false));
	
	// Check if target is a file
	if (!S_ISREG(st.st_mode))
		return (serveErrorPage(HTTP_FORBIDDEN, ctx, res, false));

	// Chek if target could be deleted
    if (std::remove(path.c_str()) != 0)
		return (serveErrorPage(HTTP_FORBIDDEN, ctx, res, false));
    return (prepareResponse(res, HTTP_OK, true));
}

bool StaticHandler::handle(HttpRequest &req, HttpResponse &res, RequestContext &ctx)
{
    // Only GET/HEAD; soft-fail others with empty body (serializer always sends 200).
    const std::string m = req.getMethod();
    const bool is_head = (m == "HEAD");

    // Some Test Cases write POST requests, that end up falling into here
    // They require this exact behaviour to be treated with 200, and continued later
    // Frankly, this should probably not be here, and looks like a band aid fix
    if (m != "GET" && m != "DELETE" && m != "HEAD") {
        return (prepareResponse(res, true));
    }

    // Prefer router/pipeline-computed paths
    const std::string base = !ctx.effective_root.empty()
        ? ctx.effective_root
        : ((ctx.loc && !ctx.loc->root.empty()) ? ctx.loc->root : ctx.vs->root);

    // Original URI (as path) -> rel always begins with '/'
    std::string rel = !ctx.rel_path.empty() ? ctx.rel_path : req.getPath();
    if (rel.empty() || rel[0] != '/') rel = "/" + rel;

    // ---------------- try_files resolution (if configured) ----------------
    // Build candidate list from location.try_files; support $uri expansion and =CODE
    int terminal_code = 0;               // e.g., =404
    std::string final_rewrite_uri;       // last token if not =CODE (internal rewrite)

    if (ctx.loc && !ctx.loc->try_files.empty()) {
        // Prepare expanded candidates
        std::vector<std::string> cand;
        cand.reserve(ctx.loc->try_files.size());

        for (size_t i = 0; i < ctx.loc->try_files.size(); ++i) {
            const std::string tok = ctx.loc->try_files[i];
            if (!tok.empty() && tok[0] == '=') {
                // terminal -> record code, skip as a FS candidate
                if (tok.size() > 1) {
                    terminal_code = std::atoi(tok.c_str() + 1);
                    if (terminal_code <= 0) terminal_code = 404;
                } else {
                    terminal_code = 404;
                }
                continue;
            }
            std::string t = tok;

            // Expand $uri -> current rel (already begins with '/')
            for (std::string::size_type pos = 0;
                 (pos = t.find("$uri", pos)) != std::string::npos; ) {
                t.replace(pos, 4, rel);
                pos += rel.size();
            }

            // Ensure it starts with '/'
            if (!t.empty() && t[0] != '/')
                t.insert(t.begin(), '/');

            cand.push_back(t);
        }

        // If last token was not =code and exists, remember it for fallback rewrite
        if (!ctx.loc->try_files.empty()) {
            const std::string &last = ctx.loc->try_files.back();
            if (!( !last.empty() && last[0] == '=' )) {
                final_rewrite_uri = last;
                // expand $uri in rewrite too
                for (std::string::size_type pos = 0;
                     (pos = final_rewrite_uri.find("$uri", pos)) != std::string::npos; ) {
                    final_rewrite_uri.replace(pos, 4, rel);
                    pos += rel.size();
                }
                if (!final_rewrite_uri.empty() && final_rewrite_uri[0] != '/')
                    final_rewrite_uri.insert(final_rewrite_uri.begin(), '/');
            }
        }

        // Probe candidates against filesystem; choose the first that exists
        // We’ll set 'rel' to the chosen URI and continue into the normal code path.
        struct FS {
            static bool exists(const std::string& p) {
                struct stat st; return ::stat(p.c_str(), &st) == 0;
            }
            static std::string join(const std::string& a, const std::string& b) {
                if (a.empty()) return b;
                if (b.empty()) return a;
                const bool as = (a[a.size()-1] == '/');
                const bool bs = (b[0] == '/');
                if (as && bs) return a + b.substr(1);
                if (as || bs)  return a + b;
                return a + "/" + b;
            }
        };

        bool picked = false;
        for (size_t i = 0; i < cand.size(); ++i) {
            const std::string full = FS::join(base, cand[i]);
            if (FS::exists(full)) {
                rel = cand[i]; // adopt this URI
                picked = true;
                break;
            }
        }

        if (!picked) {
            // No candidate existed: if terminal code given, return it.
            if (terminal_code > 0) {
                return serveErrorPage(terminal_code, ctx, res, is_head);
            }
            // Else if rewrite URI present, adopt it (even if it doesn't exist,
            // we’ll let normal path resolution (handleGet) decide 404/autoindex/index)
            if (!final_rewrite_uri.empty()) {
                rel = final_rewrite_uri;
            }
            // Otherwise, leave rel as original $uri
        }
    }

    // ---------------------------------------------------------------------

    // Build FS candidate from (possibly updated) rel
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
        return serveErrorPage(HTTP_NOT_FOUND, ctx, res, is_head);
    }

    // Update ctx.rel_path so downstream uses the resolved URI (strip leading '/')
    ctx.rel_path = (rel.size() && rel[0] == '/') ? rel.substr(1) : rel;

    if (m == "GET")
        return (handleGet(canonPath, rel, false, req, res, ctx));
    else if (m == "HEAD")
        return (handleGet(canonPath, rel, true, req, res, ctx));
    else if (m == "DELETE")
        return (handleDelete(canonPath, res, ctx));

    res.setStatus(HTTP_BAD_REQUEST);
    res.clearBody();
    res.headers.set(HDR_CONTENT_TYPE, "text/plain");
    return (true);
}
