/* --- CgiRegistry.cpp --- */

/* ------------------------------------------
author: undefined
date: 8/10/2025
------------------------------------------ */

/* --- CgiRegistry.cpp --- */
#include "CgiRegistry.h"




/* 

CgiRegistry::CgiRegistry()

Initializes the registry with local_ and global_ pointers set to NULL (0). 
The registry is intentionally non-owning: it references CGI maps that live in parsed configuration objects 
(Location, VirtualServer). Starting null makes misuse obvious (lookups will simply return NULL) 
and avoids accidental dereference before configuration is attached. 
This lightweight construction keeps the registry cheap to create per request or per router instance, 
while preserving a single, consistent lookup mechanism for extension→interpreter resolution. 
It establishes the invariant that the registry may be empty until setSources is called, 
simplifying error handling and making behavior explicit during early server startup.

*/

CgiRegistry::CgiRegistry() : local_(0), global_(0) {}
CgiRegistry::~CgiRegistry() {}


/* 

void CgiRegistry::setSources(const std::map<std::string,CgiSpec>* local, const std::map<std::string,CgiSpec>* global)

Binds the registry to two optional maps: a per-location override (local) 
and a server-wide default registry (global). Storing raw pointers (not copies) 
keeps lookups extremely fast and ensures the registry always reflects the latest parsed 
configuration without duplication. The precedence model mirrors nginx semantics: 
look in the tighter scope first (location), then fall back to broader scope (server). 
Calling this after route resolution means interpreter selection will reflect the matched location’s policies. 
If either pointer is NULL, that layer is simply skipped, 
letting you use only global defaults or only per-location rules as needed.

*/

void CgiRegistry::setSources(const std::map<std::string, CgiSpec> *local,
							 const std::map<std::string, CgiSpec> *global)
{
	local_ = local;
	global_ = global;
}



/* 

static std::string normalizeExt(const std::string& e)

Normalizes a requested extension into a canonical key used by the maps. 
If the string is empty, it stays empty. Otherwise, 
if it doesn’t start with a dot, the function prepends ., converting "php" → ".php" and leaving ".py" unchanged. 
This tiny but crucial step prevents mismatches between configuration authors who include dots and code paths 
that may strip or omit them when parsing filenames. Having a single normalization point keeps 
behavior consistent across the entire codebase and avoids duplicate entries like "php" and ".php" 
representing the same interpreter mapping, simplifying maintenance and preventing lookup bugs


*/

static std::string normalizeExt(const std::string &e)
{
	if (e.empty())
		return e;
	return (e[0] == '.') ? e : std::string(".") + e;
}



/* 

const CgiSpec* CgiRegistry::findByExtension(const std::string& raw) const

Resolves an interpreter specification from a file extension. 
First canonicalizes raw with normalizeExt. 
If a local map is present, it probes that first, returning the address of the matching CgiSpec on success. 
If not found, it checks the global map. Returning NULL indicates “no CGI mapping,” 
allowing callers (like CgiHandler) to decline CGI handling and fall back to static or other handlers. 
The pointer return avoids copies, stays cheap, and mirrors the registry’s non-owning contract. 
This layered lookup encodes override semantics (location beats server) 
while remaining O(1) average thanks to std::map’s logarithmic lookup on small sets.


*/

const CgiSpec *CgiRegistry::findByExtension(const std::string &raw) const
{
	const std::string key = normalizeExt(raw);
	if (local_)
	{
		std::map<std::string, CgiSpec>::const_iterator it = local_->find(key);
		if (it != local_->end())
			return &it->second;
	}
	if (global_)
	{
		std::map<std::string, CgiSpec>::const_iterator it = global_->find(key);
		if (it != global_->end())
			return &it->second;
	}
	return 0;
}
