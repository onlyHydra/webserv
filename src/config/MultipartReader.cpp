
#include "MultipartReader.h"
#include <cctype>



/* 

static bool starts_with(const std::string& s, const std::string& p)

Tiny utility that returns true if s begins with prefix p using a length check plus compare(0, p.size(), p). 
Multipart boundaries and end markers are fixed strings like "--<boundary>" and "--<boundary>--", 
so fast, allocation-free prefix checks are ideal. Keeping this helper local avoids repeated 
boilerplate and clarifies intent in parsing paths such as detecting boundary lines, consuming marker tokens, 
or verifying we’re looking at the correct delimiter before mutating buffers. Simplicity, inlined execution, 
and freedom from locale/encoding concerns make it safe and efficient on hot paths during large multipart uploads.


*/

static bool starts_with(const std::string &s, const std::string &p)
{
	return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}


/* 

MultipartReader::MultipartReader()

Constructor initializes the reader’s state machine to S_PREAMBLE. 
That means the parser expects optional garbage or CRLF before the very first boundary 
line. Starting here ensures robustness with diverse clients (some prepend newlines before boundaries). 
The constructor doesn’t allocate or parse; it simply sets deterministic initial state so subsequent 
feed() calls can operate safely. Keeping construction minimal allows reusing objects across requests or 
instantiating them cheaply per upload without overhead. It complements setBoundary/initFromContentType, 
which will finish configuration before parsing. Predictable baseline state is essential 
for correctness in streaming, non-blocking scenarios.

*/

MultipartReader::MultipartReader() : st_(S_PREAMBLE) {}



/* 

bool MultipartReader::ieq(char a, char b)

ASCII case-insensitive character comparison used for header token logic. 
It manually folds A–Z to lowercase via arithmetic and compares bytes. 
Multipart header names (Content-Disposition, etc.) are ASCII and case-insensitive by spec, 
so a locale-free comparison avoids surprises and overhead. 
Encapsulating the behavior avoids repeatedly calling std::tolower (locale-dependent) 
and keeps the branch-free fast path. Although small, it underpins 
reliable recognition of keys across clients that vary capitalization, 
ensuring downstream parsing (e.g., disposition analysis) sees normalized tokens and behaves consistently.

*/

bool MultipartReader::ieq(char a, char b)
{
	if (a >= 'A' && a <= 'Z')
		a = char(a - 'A' + 'a');
	if (b >= 'A' && b <= 'Z')
		b = char(b - 'A' + 'a');
	return a == b;
}

/* 

std::string MultipartReader::trim(const std::string& s)

Trims ASCII spaces and tabs from both ends. Used widely when parsing header 
lines and parameter lists (e.g., key=value pairs in Content-Disposition). 
Trimming prevents subtle bugs caused by optional whitespace permitted in RFC formatting and produces 
clean tokens for dictionary insertion or comparison. It operates by scanning indices and returning a substring, 
avoiding per-character reallocations. Normalization here simplifies subsequent 
steps like lower-casing keys and deciding whether a header is present, 
keeping the main state machine readable and the hot path fast.


*/

std::string MultipartReader::trim(const std::string &s)
{
	std::string::size_type a = 0, b = s.size();
	while (a < b && (s[a] == ' ' || s[a] == '\t'))
		++a;
	while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t'))
		--b;
	return s.substr(a, b - a);
}

/* 

bool MultipartReader::parseContentDisposition(const std::string& v, std::string& name, std::string& filename)

Parses a single Content-Disposition value, expecting "form-data" followed by semicolon-separated parameters. 
It trims and reads name="..." and optional filename="...", accepting either quoted or unquoted tokens, 
and advances carefully across semicolons. On success, fills output parameters; otherwise returns false. 
Correctly extracting name and filename is essential for routing each part to the right sink: fields vs files, 
and safe file naming downstream. The function’s conservative rules reject malformed headers early (bad content-disposition) 
to avoid corrupt state or misattributed data writes. Centralizing this logic keeps the main feed() loop straightforward.


*/

bool MultipartReader::parseContentDisposition(const std::string &v,
											  std::string &name, std::string &filename)
{
	// Expect: form-data; name="field"; filename="file"
	name.clear();
	filename.clear();
	std::string s = v;
	// find first semicolon, skip type token (must be form-data)
	std::string::size_type semi = s.find(';');
	std::string type = trim(semi == std::string::npos ? s : s.substr(0, semi));
	if (type != "form-data")
		return false;

	std::string::size_type i = (semi == std::string::npos) ? s.size() : semi + 1;
	while (i < s.size())
	{
		while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == ';'))
			++i;
		if (i >= s.size())
			break;

		std::string::size_type eq = s.find('=', i);
		if (eq == std::string::npos)
			break;
		std::string key = trim(s.substr(i, eq - i));

		std::string val;
		i = eq + 1;
		if (i < s.size() && s[i] == '"')
		{
			++i;
			while (i < s.size() && s[i] != '"')
			{
				val.push_back(s[i]);
				++i;
			}
			if (i < s.size() && s[i] == '"')
				++i;
		}
		else
		{
			while (i < s.size() && s[i] != ';')
			{
				val.push_back(s[i]);
				++i;
			}
		}
		if (key == "name")
			name = val;
		else if (key == "filename")
			filename = val;
		// advance to next ; if any
		while (i < s.size() && s[i] != ';')
			++i;
		if (i < s.size() && s[i] == ';')
			++i;
	}
	return true;
}


/* 

bool MultipartReader::initFromContentType(const std::string& ct)

Performs a minimal parse of the HTTP Content-Type header, requiring prefix "multipart/form-data" and a boundary= parameter. 
It handles optional quoted boundary values (boundary="abc123"). If either the type or boundary is missing/malformed, 
it returns false so callers can reject the request. Otherwise, it delegates to setBoundary. 
By isolating header parsing here, the class cleanly separates “configure me” from “stream data,” 
making it easy for higher-level handlers to validate inputs before feeding bytes. 
Strictness helps prevent desynchronization bugs where the parser would misinterpret bodies not matching multipart framing.


*/

bool MultipartReader::initFromContentType(const std::string &ct)
{
	// minimal parse: "multipart/form-data; boundary=XXXX"
	if (!starts_with(ct, "multipart/form-data"))
		return false;
	std::string::size_type b = ct.find("boundary=");
	if (b == std::string::npos)
		return false;
	std::string val = ct.substr(b + 9);
	// strip optional quotes
	if (!val.empty() && val[0] == '"')
	{
		if (val.size() < 2 || val[val.size() - 1] != '"')
			return false;
		val = val.substr(1, val.size() - 2);
	}
	return setBoundary(val);
}



/* 

bool MultipartReader::setBoundary(const std::string& boundary)

Validates and stores the raw boundary string, rejecting CR/LF to block injection. 
Precomputes dashBoundary_ = "--" + boundary and endBoundary_ = dashBoundary_ + "--", 
resets state to S_PREAMBLE, clears buffer and error string. 
Precomputation speeds scanning and clean state ensures reuse is safe across multiple requests if desired. 
Denying control characters guards against ambiguous parsing and potential security issues. 
Having a single initialization point improves testability and keeps the main loop focused on streaming rather than setup. 
Returns true on success so callers can proceed to feed().

*/

bool MultipartReader::setBoundary(const std::string &boundary)
{
	if (boundary.empty())
		return false;
	// Basic validation: boundary may not contain CR/LF
	for (std::size_t i = 0; i < boundary.size(); ++i)
		if (boundary[i] == '\r' || boundary[i] == '\n')
			return false;
	boundary_ = boundary;
	dashBoundary_ = std::string("--") + boundary_;
	endBoundary_ = dashBoundary_ + "--";
	st_ = S_PREAMBLE;
	buf_.clear();
	err_.clear();
	return true;
}


/* 

bool MultipartReader::findLine(std::string& out)

Scans the internal buffer for CRLF (\r\n). When found, returns the line content without CRLF and erases consumed bytes from the buffer; 
otherwise returns false (need more data). This incremental line reader powers multiple stages: skipping preamble, 
collecting part headers, and consuming boundary lines. It’s designed for non-blocking feeds where lines may arrive 
split across network packets; the buffer simply accumulates until a terminator appears. 
Keeping this logic localized reduces complexity in the state machine and avoids repeated index math scattered throughout the code.

*/

bool MultipartReader::findLine(std::string &out)
{
	// Look for CRLF; keep one line in out (without CRLF)
	for (std::size_t i = 0; i + 1 < buf_.size(); ++i)
	{
		if (buf_[i] == '\r' && buf_[i + 1] == '\n')
		{
			out.assign(buf_, 0, i);
			buf_.erase(0, i + 2);
			return true;
		}
	}
	return false;
}

/* 


bool MultipartReader::startBoundaryLine(const std::string& line, bool& isFinal) const

Validates a boundary line. Accepts exactly "--<boundary>" (regular separator) and "--<boundary>--" 
(final terminator). It first checks the prefix using dashBoundary_, then distinguishes finality 
via the trailing "--". Any deviation (extra characters, wrong prefix) returns false. 
Precision here matters: the parser relies on this to switch between part header parsing, part data streaming, 
or declaring the upload complete. Misclassification would corrupt part framing, either 
merging files or truncating data. The function keeps the rule simple and explicit, reducing parsing ambiguity.

*/

bool MultipartReader::startBoundaryLine(const std::string &line, bool &isFinal) const
{
	// Valid: "--boundary" or "--boundary--"
	if (!starts_with(line, dashBoundary_))
		return false;
	if (line.size() == dashBoundary_.size())
	{
		isFinal = false;
		return true;
	}
	if (line.size() == endBoundary_.size() && line == endBoundary_)
	{
		isFinal = true;
		return true;
	}
	return false;
}


/* 

std::size_t MultipartReader::feed(const char* data, std::size_t n, IMultipartSink* sink)

Streaming engine. Appends bytes to buf_ and advances a state machine:

S_PREAMBLE: read lines until a valid boundary is found; if final boundary appears immediately, mark S_DONE.

S_HEADERS: read header lines until empty line; lower-case keys; require content-disposition; parse it; call sink->onPartBegin(cur_).

S_DATA: search for the earliest \r\n--<boundary> or \r\n--<boundary>--; stream bytes before marker to onPartData; consume marker, CRLF (unless final), call onPartEnd; then either S_HEADERS (next part) or S_DONE.
It emits “safe” chunks (leaving overlap bytes) when no marker yet. Errors set err_ and S_ERROR. Returns bytes fed.

*/

std::size_t MultipartReader::feed(const char *data, std::size_t n, IMultipartSink *sink)
{
	if (!data || n == 0 || !sink)
		return 0;
	std::size_t fed = 0;
	buf_.append(data, n);
	fed = n;

	while (!buf_.empty() && st_ != S_DONE && st_ != S_ERROR)
	{
		if (st_ == S_PREAMBLE)
		{
			std::string line;
			if (!findLine(line))
				break; // need more
			bool finalB = false;
			if (!startBoundaryLine(line, finalB))
				continue; // skip preamble junk lines
			if (finalB)
			{
				st_ = S_DONE;
				break;
			}
			st_ = S_HEADERS;
			cur_ = IMultipartSink::Part(); // reset
		}
		else if (st_ == S_HEADERS)
		{
			// read header lines until empty line
			std::string line;
			std::map<std::string, std::string> hdrs;
			for (;;)
			{
				if (!findLine(line))
					return fed; // need more
				if (line.empty())
					break; // end headers
				std::string::size_type c = line.find(':');
				if (c == std::string::npos)
				{
					setError("malformed part header");
					return fed;
				}
				std::string k = trim(line.substr(0, c));
				std::string v = trim(line.substr(c + 1));
				// lower-case keys (simple)
				for (std::size_t i = 0; i < k.size(); ++i)
					if (k[i] >= 'A' && k[i] <= 'Z')
						k[i] = char(k[i] - 'A' + 'a');
				hdrs[k] = v;
			}
			cur_.headers = hdrs;

			std::map<std::string, std::string>::const_iterator it = hdrs.find("content-disposition");
			if (it == hdrs.end())
			{
				setError("no content-disposition");
				return fed;
			}
			if (!parseContentDisposition(it->second, cur_.name, cur_.filename))
			{
				setError("bad content-disposition");
				return fed;
			}
			if (!sink->onPartBegin(cur_))
			{
				setError("sink refused part");
				return fed;
			}
			st_ = S_DATA;
		}
		else if (st_ == S_DATA)
		{
			// Search for "\r\n--boundary"
			// Keep at least (dashBoundary_.size()+4) bytes to safely search
			const std::string marker1 = std::string("\r\n") + dashBoundary_;
			const std::string marker2 = std::string("\r\n") + endBoundary_;

			// find earliest marker occurrence
			std::string::size_type m1 = buf_.find(marker1);
			std::string::size_type m2 = buf_.find(marker2);
			std::string::size_type m = std::string::npos;

			if (m1 != std::string::npos)
				m = m1;
			if (m2 != std::string::npos && (m == std::string::npos || m2 < m))
				m = m2;

			if (m == std::string::npos)
			{
				// emit everything except the last (marker length) bytes, to allow overlap detection
				std::size_t safe = (buf_.size() > marker1.size()) ? (buf_.size() - marker1.size()) : 0;
				if (safe)
				{
					if (!sink->onPartData(buf_.data(), safe))
					{
						setError("sink write");
						return fed;
					}
					buf_.erase(0, safe);
				}
				break; // need more data
			}

			// emit data up to marker
			if (m > 0)
			{
				if (!sink->onPartData(buf_.data(), m))
				{
					setError("sink write");
					return fed;
				}
				buf_.erase(0, m);
			}

			// we are at "\r\n--boundary" or "\r\n--boundary--"
			// consume CRLF
			if (buf_.size() >= 2 && buf_[0] == '\r' && buf_[1] == '\n')
				buf_.erase(0, 2);

			// consume --boundary
			if (starts_with(buf_, dashBoundary_))
				buf_.erase(0, dashBoundary_.size());

			// final?
			bool isFinal = false;
			if (starts_with(buf_, "--"))
			{
				isFinal = true;
				buf_.erase(0, 2);
			}

			// consume the trailing CRLF after boundary line (unless final end)
			if (!isFinal)
			{
				if (buf_.size() >= 2 && buf_[0] == '\r' && buf_[1] == '\n')
					buf_.erase(0, 2);
			}

			if (!sink->onPartEnd())
			{
				setError("sink close");
				return fed;
			}
			if (isFinal)
			{
				st_ = S_DONE;
			}
			else
			{
				st_ = S_HEADERS;
				cur_ = IMultipartSink::Part();
			}
		}
		else if (st_ == S_AFTER)
		{
			st_ = S_DONE;
		}
		else
		{
			break;
		}
	}
	return fed;
}
