#include "ServerConfig.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <cctype>
#include <cstdlib>
#include <set>

// ---------- tiny helpers (only those we use) ----------
static bool parseUnsigned(const std::string &s, int &out) {
    if (s.empty()) return false;
    long v = 0;
    for (std::string::size_type i = 0; i < s.size(); ++i) {
        if (s[i] < '0' || s[i] > '9') return false;
        v = v * 10 + (s[i] - '0');
        if (v > 1000000L) return false; // guard
    }
    out = static_cast<int>(v);
    return true;
}

static void validateErrorStatusOrThrow(const std::string &raw) {
    int code = -1;
    if (!parseUnsigned(raw, code) || code < 400 || code > 599) {
        std::ostringstream oss;
        oss << "invalid error_page status code: '" << raw << "'";
        throw std::runtime_error(oss.str());
    }
}

// ---------- ServerConfig impl ----------

ServerConfig::ServerConfig()
: _servers()
, session_enabled(false)
, session_cookie_name()
, session_max_age(0)
, session_secure(false)
, session_http_only(false)
, session_same_site()
, mime_mapping()
, cgi_defaults()
{}

ServerConfig::~ServerConfig() {}

void ServerConfig::push_back(const VirtualServer &vs) {
    _servers.push_back(vs);
}

bool ServerConfig::canOpen(const char *path) const {
    std::ifstream f(path);
    return f.good();
}

std::string ServerConfig::readWholeFile(const std::string &path) {
    std::ifstream f(path.c_str(), std::ios::in | std::ios::binary);
    if (!f) {
        throw std::runtime_error("cannot open config: " + path);
    }
    std::ostringstream oss;
    oss << f.rdbuf();
    return oss.str();
}

std::vector<std::string> ServerConfig::tokenize(const std::string &data) {
    std::vector<std::string> out;
    std::string cur;

    for (std::string::size_type i = 0; i < data.size();) {
        char ch = data[i];

        // comments start with '#': skip until newline
        if (ch == '#') {
            while (i < data.size() && data[i] != '\n') ++i;
            continue;
        }

        // whitespace splits tokens
        if (std::isspace(static_cast<unsigned char>(ch))) {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
            ++i;
            continue;
        }

        // single-character tokens
        if (ch == '{' || ch == '}' || ch == ';') {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
            out.push_back(std::string(1, ch));
            ++i;
            continue;
        }

        // otherwise collect into current word
        cur.push_back(ch);
        ++i;
    }

    if (!cur.empty()) out.push_back(cur);
    return out;
}

void ServerConfig::parseTokens(const std::vector<std::string> &tok) {
    _servers.clear();

    const std::size_t N = tok.size();
    std::size_t i = 0;

    while (i < N) {
        const std::string &t = tok[i];

        if (t == "server") {
            if (i + 1 >= N || tok[i + 1] != "{")
                throw std::runtime_error("expected '{' after 'server'");
            i += 2; // skip "server" "{"

            VirtualServer vs; // ctor sets safe defaults

            bool seen_listen = false;

            while (i < N && tok[i] != "}") {
                const std::string &kw = tok[i];

                // listen
                if (kw == "listen") {
                    if (i + 1 >= N)
                        throw std::runtime_error("listen expects 'PORT' or 'HOST:PORT'");

                    std::string a = tok[i + 1];
                    std::string b;
                    std::size_t adv = 2;

                    // Two-token form? "listen HOST PORT ;"
                    if (i + 2 < N && tok[i + 2] != ";" && tok[i + 2] != "{" && tok[i + 2] != "}") {
                        b = tok[i + 2];
                        adv = 3;
                    }
                    if (i + adv < N && tok[i + adv] == ";") ++adv;

                    std::string host;
                    int port = -1;

                    if (b.empty()) {
                        // one token form: "PORT" or "HOST:PORT"
                        std::string::size_type cpos = a.find(':');
                        if (cpos == std::string::npos) {
                            char *endp = 0;
                            long p = ::strtol(a.c_str(), &endp, 10);
                            if (!a.size() || (endp && *endp) || p <= 0 || p > 65535)
                                throw std::runtime_error("invalid listen port");
                            port = static_cast<int>(p);
                            host.clear(); // wildcard
                        } else {
                            host = a.substr(0, cpos);
                            std::string pstr = a.substr(cpos + 1);
                            char *endp = 0;
                            long p = ::strtol(pstr.c_str(), &endp, 10);
                            if (!pstr.size() || (endp && *endp) || p <= 0 || p > 65535)
                                throw std::runtime_error("invalid listen port");
                            port = static_cast<int>(p);
                        }
                    } else {
                        // two-token form: HOST PORT
                        host = a;
                        char *endp = 0;
                        long p = ::strtol(b.c_str(), &endp, 10);
                        if (!b.size() || (endp && *endp) || p <= 0 || p > 65535)
                            throw std::runtime_error("invalid listen port");
                        port = static_cast<int>(p);
                    }

                    vs.listen_host = host;
                    vs.listen_port = port;
                    seen_listen = true;
                    i += adv;
                    continue;
                }

                // server_name name1 name2 ... ;
                if (kw == "server_name") {
                    std::size_t j = i + 1;
                    if (j >= N)
                        throw std::runtime_error("server_name expects at least one name");
                    for (; j < N && tok[j] != ";"; ++j) {
                        vs.server_names.push_back(tok[j]);
                    }
                    if (j >= N || tok[j] != ";")
                        throw std::runtime_error("expected ';'");
                    i = j + 1;
                    continue;
                }

                // root /path ;
                if (kw == "root") {
                    if (i + 1 >= N)
                        throw std::runtime_error("root expects a path");
                    vs.root = tok[i + 1];
                    if (i + 2 < N && tok[i + 2] == ";")
                        i += 3;
                    else
                        i += 2;
                    continue;
                }

                // index file1 file2 ... ;
                if (kw == "index") {
                    std::size_t j = i + 1;
                    if (j >= N)
                        throw std::runtime_error("index expects at least one file");
                    for (; j < N && tok[j] != ";"; ++j) {
                        vs.index_files.push_back(tok[j]);
                    }
                    if (j >= N || tok[j] != ";")
                        throw std::runtime_error("expected ';'");
                    i = j + 1;
                    continue;
                }

                // error_page <STATUS> <PATH> [;]
                if (kw == "error_page") {
                    if (i + 2 >= N)
                        throw std::runtime_error("error_page expects: <status> <path>");

                    const std::string &statusTok = tok[i + 1];
                    validateErrorStatusOrThrow(statusTok);

                    char *endp = 0;
                    long st = ::strtol(statusTok.c_str(), &endp, 10);
                    if (!statusTok.size() || (endp && *endp))
                        throw std::runtime_error("invalid error code");

                    int status = static_cast<int>(st);
                    const std::string &epath = tok[i + 2];

                    std::size_t adv = 3;
                    if (i + 3 < N && tok[i + 3] == ";") ++adv;

                    vs.error_pages[status] = epath;
                    i += adv;
                    continue;
                }

                // location <PATH> { ... }
                if (kw == "location") {
                    if (i + 2 >= N)
                        throw std::runtime_error("location expects: <path> {");
                    const std::string &locPath = tok[i + 1];
                    if (tok[i + 2] != "{")
                        throw std::runtime_error("expected '{' after location path");
                    i += 3;

                    Location loc; // constructor sets defaults
                    loc.path_prefix = locPath;

                    while (i < N && tok[i] != "}") {
                        const std::string &lkw = tok[i];

                        // location root
                        if (lkw == "root") {
                            if (i + 1 >= N)
                                throw std::runtime_error("location root expects a path");
                            loc.root = tok[i + 1];
                            if (i + 2 < N && tok[i + 2] == ";")
                                i += 3;
                            else
                                i += 2;
                            continue;
                        }

                        // location index file1 file2 ... ;
                        if (lkw == "index") {
                            std::size_t j = i + 1;
                            if (j >= N)
                                throw std::runtime_error("location index expects at least one file");
                            for (; j < N && tok[j] != ";"; ++j) {
                                loc.index_files.push_back(tok[j]);
                            }
                            if (j >= N || tok[j] != ";")
                                throw std::runtime_error("expected ';'");
                            i = j + 1;
                            continue;
                        }

                        // location autoindex on|off ;
                        if (lkw == "autoindex") {
                            if (i + 1 >= N)
                                throw std::runtime_error("autoindex expects 'on' or 'off'");
                            const std::string &v = tok[i + 1];
                            if (v == "on")       loc.autoindex = true;
                            else if (v == "off") loc.autoindex = false;
                            else
                                throw std::runtime_error("invalid autoindex value (use 'on' or 'off')");
                            if (i + 2 < N && tok[i + 2] == ";")
                                i += 3;
                            else
                                i += 2;
                            continue;
                        }

                        // location methods GET POST DELETE ;
                        if (lkw == "methods") {
                            std::size_t j = i + 1;
                            if (j >= N)
                                throw std::runtime_error("methods expects at least one method");
                            for (; j < N && tok[j] != ";"; ++j) {
                                const std::string m = tok[j];
                                if (!(m == "GET" || m == "POST" || m == "DELETE")) {
                                    std::ostringstream oss;
                                    oss << "unsupported HTTP method: " << m;
                                    throw std::runtime_error(oss.str());
                                }
                                loc.allowed_methods.push_back(m);
                            }
                            if (j >= N || tok[j] != ";")
                                throw std::runtime_error("expected ';'");
                            i = j + 1;
                            continue;
                        }

                        // Unknown directive inside location
                        std::string msg = "unknown directive in location: ";
                        msg += lkw;
                        throw std::runtime_error(msg);
                    }

                    if (i >= N || tok[i] != "}")
                        throw std::runtime_error("missing '}' to close location block");

                    ++i; // consume '}'
                    vs.locations.push_back(loc);
                    continue;
                }

                // Unknown directive inside server
                std::string msg = "unknown directive in server block: ";
                msg += kw;
                throw std::runtime_error(msg);
            }

            if (i >= N || tok[i] != "}")
                throw std::runtime_error("missing '}' to close server block");

            // validate required directives in server
            if (!seen_listen || vs.listen_port <= 0)
                throw std::runtime_error("server missing valid listen directive");

            ++i; // consume server '}'
            _servers.push_back(vs);
            continue;
        }

        if (t == ";") { ++i; continue; } // stray semicolon tolerated

        // Unknown top-level directive
        std::string msg = "unknown top-level directive: ";
        msg += t;
        throw std::runtime_error(msg);
    }
}

void ServerConfig::checkDuplicateListen_() const {
    std::set< std::pair<std::string,int> > seen;
    for (std::vector<VirtualServer>::size_type i = 0; i < _servers.size(); ++i) {
        const VirtualServer &vs = _servers[i];
        const int port = vs.listen_port;
        if (port <= 0 || port > 65535) continue;
        const std::string host = vs.listen_host.empty() ? std::string("0.0.0.0") : vs.listen_host;
        const std::pair<std::string,int> key(host, port);
        if (!seen.insert(key).second) {
            std::ostringstream oss;
            oss << "duplicate listen directive: " << host << ":" << port;
            throw std::runtime_error(oss.str());
        }
    }
}

void ServerConfig::parseFile(const std::string &path) {
    const std::string data = readWholeFile(path);
    parseString(data);
}

void ServerConfig::parseString(const std::string &text) {
    const std::vector<std::string> tok = tokenize(text);
    if (tok.empty()) { _servers.clear(); return; }
    parseTokens(tok);
    checkDuplicateListen_();
}

