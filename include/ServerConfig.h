/* --- ServerConfig.h --- */

/* ------------------------------------------
Author: undefined
Date: 8/10/2025
------------------------------------------ */

#ifndef SERVERCONFIG_H
#define SERVERCONFIG_H

#include <string>
#include <vector>
#include <map>

#include "VirtualServer.h"   // VirtualServer + Location/CgiSpec models

class ServerConfig
{
public:
    ServerConfig();
    ~ServerConfig();

    // IO / entry points
    bool canOpen(const char *path) const;
    void parseFile(const std::string &path);

    // Optional convenience if you want to parse from an in-memory string
    void parseString(const std::string &text);

    // Accessors
    const std::vector<VirtualServer>& servers() const { return _servers; }
    void push_back(const VirtualServer &vs);

private:
    // Helpers
    static std::string              readWholeFile(const std::string &path);
    static std::vector<std::string> tokenize(const std::string &data);
    void                            parseTokens(const std::vector<std::string> &tok);
    void                            checkDuplicateListen_() const;

private:
    std::vector<VirtualServer> _servers;

    // (Optional) extras aligning to your diagram — not used by tests
public:
    bool                             session_enabled;
    std::string                      session_cookie_name;
    int                              session_max_age;
    bool                             session_secure;
    bool                             session_http_only;
    std::string                      session_same_site;
    std::map<std::string,std::string> mime_mapping;
    std::map<std::string,CgiSpec>      cgi_defaults;
};

#endif // SERVERCONFIG_H


