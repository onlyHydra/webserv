#include "HttpPreconditions.h"

/**
 * @brief Returns given string stripped at front and end of spaces and tabs
 * @param s String to strip
 * @returns the result of stripping
 */



 /* 
 
 static std::string trim_ws(const std::string& s)

Removes leading and trailing ASCII spaces or tabs. 
HTTP conditional headers (e.g., If-None-Match: "etag1", "etag2") often include optional whitespace around commas and tokens. 
Normalizing tokens before comparison prevents spurious mismatches and makes later parsing predictable. 
This helper is tiny, allocation-light (returns a substring copy), and avoids locale complexity. 
It is called from both comma splitting and ETag comparison paths, 
ensuring consistent cleanliness across inputs from various clients and proxies. 
Without trimming, weak/strong ETag comparisons and date parsing could fail on harmless 
formatting differences, producing incorrect cache or precondition decisions.
 
 */


static std::string trim_ws(const std::string& s) 
{
    std::string::size_type a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t'))
		++a;
    while (b > a && (s[b-1] == ' ' || s[b-1] == '\t'))
		--b;
    return s.substr(a, b - a);
}

/**
 * Splits string into substrings, based on commas
 * Removes spaces and tabs before and after commas
 */


 /*  
 
	static void split_commas(const std::string& s, std::vector<std::string>& out)

	Splits a comma-separated header value into individual tokens and trims each token with trim_ws. 
	Used for headers like If-None-Match and If-Match, which allow lists of validators (e.g., "etag1", W/"etag2"). 
	Producing a clean vector simplifies subsequent iteration and comparison without repeatedly scanning the original string. 
	This function deliberately avoids regex and complex parsing for performance and portability. 
	Having a single, well-defined splitter keeps behavior consistent across different precondition checks, 
	preventing subtle bugs where one path treats whitespace or empty items differently from another. 
	It’s the foundation for reliable multi-ETag evaluation.
 
 */



/**
 * Splits string into substrings, based on commas
 * Removes spaces and tabs before and after commas
 */


 /*  
 
	static void split_commas(const std::string& s, std::vector<std::string>& out)

	Splits a comma-separated header value into individual tokens and trims each token with trim_ws. 
	Used for headers like If-None-Match and If-Match, which allow lists of validators (e.g., "etag1", W/"etag2"). 
	Producing a clean vector simplifies subsequent iteration and comparison without repeatedly scanning the original string. 
	This function deliberately avoids regex and complex parsing for performance and portability. 
	Having a single, well-defined splitter keeps behavior consistent across different precondition checks, 
	preventing subtle bugs where one path treats whitespace or empty items differently from another. 
	It’s the foundation for reliable multi-ETag evaluation.
 
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


/* 

static bool parse_http_date_rfc1123(const std::string& s, std::time_t& out)

Parses an RFC-1123 HTTP date like Wed, 03 Sep 2025 15:56:30 GMT into a UTC time_t. 
On POSIX systems, it uses strptime with the exact format, then timegm (or mktime fallback) to get a timestamp. 
Returning false on parse failure protects callers from trusting malformed dates. 
Correct parsing is crucial for If-Modified-Since handling; an invalid date must not incorrectly pass/deny preconditions. 
By zero-initializing tm fields and requiring full string consumption, 
the function avoids lenient parsing pitfalls and locale surprises, yielding deterministic behavior across platforms.

*/

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
    if (!p || *p != '\0')
		return false;

    // Use timegm (UTC) when available.
    #if defined(_GNU_SOURCE) || defined(__USE_MISC) || defined(__linux__)
        time_t t = ::timegm(&tmv);
        if (t == (time_t)-1)
			return false;
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


/* 

bool HttpPreconditions::checkIsModifiedSince(const HttpRequest& req, std::time_t mtime)

Implements the If-Modified-Since predicate. If the header is absent, returns true (no constraint). 
If present but unparseable, returns false (treat as failing precondition to be conservative). 
When parsed, compares file mtime with the client’s timestamp: if the resource is newer (mtime > since), 
returns false (precondition fails ⇒ should consider sending fresh content); 
otherwise true (not modified since, precondition holds). 
Callers invert/interpret this alongside If-None-Match precedence. 
This careful behavior prevents inaccurate 304 responses and ensures clients receive updates when files change, 
while respecting HTTP semantics prioritizing ETag over date.

*/

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

/* 

bool HttpPreconditions::checkIfNoneMatch(const HttpRequest& req, const std::string& etag)

Evaluates If-None-Match. If the header is empty, returns true (no constraint). 
If it equals "*", returns true when the resource lacks an ETag (not accessible/unknown), 
otherwise false (match found). Otherwise, splits the list and performs weak comparison (W/ allowed) 
against the current ETag: if any match, returns false; if none, true. 
For GET/HEAD, a false result typically leads to 304 Not Modified; for methods like POST, 
semantics differ, but this function only reports predicate truth. 
Using weak comparison matches HTTP caching rules while letting strong validation occur elsewhere.

*/

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


/* 

bool HttpPreconditions::checkIfMatch(const HttpRequest& req, const std::string& givenEtag)

Evaluates If-Match. If absent, returns true (no constraint). 
If value is "*", returns true when the resource exists (has an ETag), 
otherwise **false. For a list, it splits and performs **strong** comparison against
givenEtag(noW/weakening). Returns **true** if any strong match is found; **false** otherwise. 
This predicate is important for write methods (PUT/PATCH/DELETE) to protect against lost updates: 
clients can ensure they’re modifying exactly the version they retrieved. 
Using strong comparison follows RFC requirements:If-Match` must use strong 
validators to avoid overwriting on semantically equivalent but not identical representations.

*/

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


/* 

bool HttpPreconditions::getPreconditons(const HttpRequest& req, const std::string& etag, const std::time_t& mtime)

Combines GET/HEAD preconditions. First evaluates If-None-Match: if it fails (i.e., a match exists), 
returns false to signal 304 Not Modified should be considered. 
Only if If-None-Match is absent does it consult If-Modified-Since: when the resource hasn’t changed since the provided date 
(checkIsModifiedSince returns true), it returns false (again indicating 304). Otherwise returns true (preconditions pass; proceed to send/compute body). 
This ordering follows the RFC: ETag validation takes precedence over modification dates, 
providing stronger correctness and avoiding time skew issues. 
Centralizing the combination prevents duplicated, inconsistent logic across handlers.


*/

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


/* 


bool HttpPreconditions::putpatchPreconditons(const HttpRequest& req, const std::string& etag)

Combines preconditions typically used for PUT/PATCH. It first checks If-Match (strong): if it fails (no exact match), 
returns false to reject the write (commonly 412 Precondition Failed). 
Then checks If-None-Match: if it fails (match exists), 
also returns false—useful to protect against unintended overwrites when a client expects no current representation. 
Only when both validations pass (or are absent appropriately) does it return **true`, 
allowing the write to proceed. This dual check implements optimistic concurrency control aligned with HTTP semantics, 
minimizing lost updates and race conditions during concurrent file modifications.

*/

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
