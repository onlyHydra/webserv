#include "RouteResolver.h"
#include <cstddef> // size_t

/**
 * @brief Checks if a string starts with the specified prefix.
 *
 * This function compares the beginning of the string @p s with the string @p prefix.
 * It returns true if @p s starts with @p prefix, and false otherwise.
 *
 * @param s The string to check.
 * @param prefix The prefix to look for at the start of @p s.
 * @return true if @p s starts with @p prefix, false otherwise.
 */
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

const Location *RouteResolver::matchLocation(const VirtualServer &vs,
											 const std::string &path,
											 std::string &matched_prefix)
{
	matched_prefix.clear();
	const Location *best = 0;
	std::string::size_type best_len = 0;

	for (std::vector<Location>::const_iterator it = vs.locations.begin();
		 it != vs.locations.end(); ++it)
	{
		const Location &L = *it;

		if (L.regex)
		{
			continue;
		}

		const std::string &pfx = L.path_prefix;
		if (pfx.empty())
			continue;

		if (pfx == path)
		{
			matched_prefix = pfx;
			return &L;
		}

		if (starts_with(path, pfx) && pfx.size() > best_len)
		{
			best = &L;
			best_len = pfx.size();
			matched_prefix = pfx;
		}
	}
	return best;
}