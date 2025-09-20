#include "ExpectContinue.h"
#include "ChainBuf.h"
#include "Headers.h"
#include <string>
#include <cctype>

// trim helpers (std98)
static std::string ltrim(const std::string &s)
{
	std::string::size_type i = 0, n = s.size();
	while (i < n && std::isspace(static_cast<unsigned char>(s[i])))
		++i;
	return s.substr(i);
}
static std::string rtrim(const std::string &s)
{
	if (s.empty())
		return s;
	std::string::size_type i = s.size();
	while (i > 0 && std::isspace(static_cast<unsigned char>(s[i - 1])))
		--i;
	return s.substr(0, i);
}
static std::string trim(const std::string &s) { return rtrim(ltrim(s)); }

static std::string to_lower(const std::string &s)
{
	std::string out(s);
	for (std::string::size_type i = 0; i < out.size(); ++i)
		out[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(out[i])));
	return out;
}

bool ExpectContinue::needed(const Headers &h)
{
	const std::string &ex = h.get("Expect");
	if (ex.empty())
		return false;

	std::string v = to_lower(ex);
	std::string::size_type start = 0;
	while (start <= v.size())
	{
		std::string::size_type comma = v.find(',', start);
		std::string tok = (comma == std::string::npos) ? v.substr(start) : v.substr(start, comma - start);
		tok = trim(tok);
		if (tok == "100-continue")
			return true;
		if (comma == std::string::npos)
			break;
		start = comma + 1;
	}
	return false;
}

void ExpectContinue::write100(ChainBuf &out)
{
	static const char kMsg[] = "HTTP/1.1 100 Continue\r\n\r\n";
	(void)out.push_copy(kMsg, sizeof(kMsg) - 1);
}