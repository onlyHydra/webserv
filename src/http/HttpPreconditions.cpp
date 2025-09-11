#include "HttpPreconditions.h"

/**
 * @brief Returns given string stripped at front and end of spaces and tabs
 * @param s String to strip
 * @returns the result of stripping
 */
static std::string trim_ws(const std::string& s) {
    std::string::size_type a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
    while (b > a && (s[b-1] == ' ' || s[b-1] == '\t')) --b;
    return s.substr(a, b - a);
}

/**
 * Splits string into substrings, based on commas
 * Removes spaces and tabs before and after commas
 */
static void split_commas(const std::string& s, std::vector<std::string>& out) {
    out.clear();
    std::string cur;
    for (std::string::size_type i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == ',') {
            out.push_back(trim_ws(cur));
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty())
        out.push_back(trim_ws(cur));
}

// RFC 1123 date → time_t (UTC). Example: "Wed, 03 Sep 2025 15:56:30 GMT"
static bool parse_http_date_rfc1123(const std::string& s, std::time_t& out) {
    // Prepare a zeroed tm without memset (memset is forbidden).
    struct tm tmv;
    tmv.tm_sec   = 0;
    tmv.tm_min   = 0;
    tmv.tm_hour  = 0;
    tmv.tm_mday  = 0;
    tmv.tm_mon   = 0;
    tmv.tm_year  = 0;
    tmv.tm_wday  = 0;
    tmv.tm_yday  = 0;
    tmv.tm_isdst = 0;

#if defined(_XOPEN_SOURCE) || defined(_GNU_SOURCE) || defined(__linux__)
    // POSIX strptime is widely available on Linux.
    const char* p = ::strptime(s.c_str(), "%a, %d %b %Y %H:%M:%S GMT", &tmv);
    if (!p || *p != '\0') return false;

    // Use timegm (UTC) when available.
    #if defined(_GNU_SOURCE) || defined(__USE_MISC) || defined(__linux__)
        time_t t = ::timegm(&tmv);
        if (t == (time_t)-1) return false;
        out = t;
        return true;
    #else
        // Fallback: mktime (local time). Correct if server TZ is UTC.
        tmv.tm_isdst = -1;
        time_t t = ::mktime(&tmv);
        if (t == (time_t)-1) return false;
        out = t;
        return true;
    #endif
#else
    (void)tmv; (void)out;
    return false;
#endif
}

// ---- public API -----------------------------------------------------

bool HttpPreconditions::checkIsModifiedSince(const HttpRequest& req,
									std::time_t mtime)
{
	const Headers &head = req.getHeaders();
	const std::string ims = head.get(HDR_IF_MODIFIED_SINCE);

	// If No such Condition Header, always true
	if (!head.keyExists(HDR_IF_MODIFIED_SINCE))
		return (true);
	
	// Get Modification Time
	std::time_t since = 0;
	if (!parse_http_date_rfc1123(ims, since))
		return (false);
	
	// Check if Modification is newer then checked time
	if (mtime > since)
		return (false);
	return (true);
}

bool HttpPreconditions::checkIfNoneMatch(const HttpRequest &req, const std::string &etag)
{
	const Headers &h = req.getHeaders();
	const std::string &inm = h.get(HDR_IF_NONE_MATCH);
	std::vector<std::string> etags;

	// If No such Condition Header, always true
	if (inm.empty())
		return (true);
	
	// If Wildcard
	if (inm == "*")
	{
		// If Resource is not accesible, Condition valid -> true
		if (etag.empty())
			return (true);
		return (false);
	}
	
	// Check if ETag matches the ones in the Request Header
	split_commas(inm, etags);
	for (std::vector<std::string>::size_type i = 0; i < etags.size(); ++i)
	{
		if (ETagUtil::weakComp(trim_ws(etags[i]), etag))
			return (false);
	}

	// No Matches for ETag with the Requests Header
	return (true);
}

bool HttpPreconditions::checkIfMatch(const HttpRequest &req, const std::string &givenEtag)
{
	std::vector<std::string> etagArray;
	const Headers &h = req.getHeaders();
	const std::string &storedEtags = h.get(HDR_IF_MATCH);

	// If no Key for checking Matching Etags
	if (!h.keyExists(HDR_IF_MATCH))
		return (true);
	
	// Any Matches
	if (storedEtags == "*")
	{
		// If Resource is not accesible, Condition invalid -> false
		if (givenEtag.empty())
			return (false);
		return (true);
	}

	// Go through all etags stored in the Header
	split_commas(storedEtags, etagArray);
	for (std::vector<std::string>::const_iterator it = etagArray.begin(); it != etagArray.end(); it++)
	{
		// If one matches the one we search -> true
		if (ETagUtil::strongComp(*it, givenEtag))
			return (true);
	}
	// No match found -> Given ETag is invalid
	return (false);
}

bool HttpPreconditions::getPreconditons(const HttpRequest &req, const std::string &etag, const std::time_t &mtime)
{
	// If ETag matches known one -> false, 304 response
	if (!HttpPreconditions::checkIfNoneMatch(req, etag))
		return (false);

	// If-None-Match Header should overwrite behaviour of If-Modified-Since
	if (!req.getHeaders().keyExists(HDR_IF_NONE_MATCH))
	{
		// If file was not modified since check -> false, 304 response
		if (!HttpPreconditions::checkIsModifiedSince(req, mtime))
			return (false);
	}
	return (true);
}

bool	HttpPreconditions::putpatchPreconditons(const HttpRequest &req, const std::string &etag)
{
	// If ETag doesnt match known one -> false, 304 response
	if (!HttpPreconditions::checkIfMatch(req, etag))
		return (false);
	
	// If ETag matches known one -> false, 304 response
	if (!HttpPreconditions::checkIfNoneMatch(req, etag))
		return (false);
	return (true);
}
