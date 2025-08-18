// ConfigParser.h
#pragma once
#include <istream>
#include <sstream>
#include "ServerConfig.h"

class ConfigParser {
public:
    ServerConfig parse(std::istream& in) {
        std::ostringstream oss;
        oss << in.rdbuf();
        ServerConfig cfg;
        cfg.parseString(oss.str());   // add this wrapper to ServerConfig (see below)
        return cfg;
    }
};
