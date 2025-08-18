#include "RouteResolver.h"
#include <cstddef> // size_t

// Tiny helper (C++98)
static bool starts_with(const std::string &s, const std::string &prefix)
{
	if (prefix.size() > s.size())
		return false;
	for (std::string::size_type i = 0; i < prefix.size(); ++i)
		if (s[i] != prefix[i])
			return false;
	return true;
}

const Location *RouteResolver::matchLocation(const VirtualServer &vs,
											 const std::string &path)
{
	// Policy (nginx-like but simplified):
	// - Ignore regex locations for now (no std::regex in C++98).
	// - Exact match wins immediately.
	// - Otherwise, choose the longest prefix match (first declared on ties).
	const Location *best = 0;
	std::string::size_type best_len = 0;

	for (std::vector<Location>::const_iterator it = vs.locations.begin();
		 it != vs.locations.end(); ++it)
	{
		const Location &L = *it;

		if (L.regex)
		{
			// TODO: regex not implemented in this minimal version
			continue;
		}

		const std::string &pfx = L.path_prefix;
		if (pfx.empty())
			continue;

		// Exact match beats everything
		if (pfx == path)
			return &L;

		// Longest prefix
		if (starts_with(path, pfx) && pfx.size() > best_len)
		{
			best = &L;
			best_len = pfx.size();
		}
	}
	return best; // may be NULL
}