/* --- CgiRegistry.h --- */

/* ------------------------------------------
Author: undefined
Date: 8/10/2025
------------------------------------------ */

/* --- CgiRegistry.h --- */
#ifndef CGIREGISTRY_H
#define CGIREGISTRY_H

#include <map>
#include <string>
#include "VirtualServer.h" 

class CgiRegistry {
public:
    CgiRegistry();
    ~CgiRegistry();

    // Point to the maps you already have:
    //   local  -> Location::cgi_by_ext
    //   global -> ServerConfig::cgi_defaults
    void setSources(const std::map<std::string, CgiSpec>* local,
                    const std::map<std::string, CgiSpec>* global);

    // Find by extension. Accepts ".php" or "php". Returns NULL if not found.
    const CgiSpec* findByExtension(const std::string& ext) const;

private:
    const std::map<std::string, CgiSpec>* local_;
    const std::map<std::string, CgiSpec>* global_;
};

#endif



