#ifndef VIRTUALSERVER_H
#define VIRTUALSERVER_H

#include <string>
#include <vector>
#include <map>

// ---------- small config structs ----------
struct CgiSpec {
		std::string bin;
		int         timeout_ms;
		CgiSpec() : bin(""), timeout_ms(0) {}
		CgiSpec(const std::string& bin, int timeout_ms) : bin(bin), timeout_ms(timeout_ms) {}
		CgiSpec(const CgiSpec& obj) : bin(obj.bin), timeout_ms(obj.timeout_ms) {}
};

struct RateLimitConfig {
		int  requests_per_minute;
		int  burst;
		bool enabled;
};

struct PutPatchConfig {
		std::string root_directory;
		int         max_body_bytes;
		bool        allow_put;
		bool        allow_patch;
		bool        generate_etag;
};

// ---------- upstream (minimal needed by tests) ----------
struct Upstream {
		std::string host;
		int         port;
		bool        healthy;
		int         weight;

		Upstream();
		~Upstream();
};

struct UpstreamPool {
		std::vector<Upstream> nodes;
		std::string           strategy;          // e.g. "roundrobin"
		std::string           health_path;
		int                   health_interval_ms;

		UpstreamPool();
		~UpstreamPool();
};

// ---------- Location ----------
struct Location {
	std::string                     path_prefix;
	bool                            regex;
	std::string                     root;
	bool                            autoindex;
	std::vector<std::string>        index_files;
	std::vector<std::string>        allowed_methods;
	std::string                     upload_dir;
	bool                            is_proxy;
	std::string                     proxy_name;
	std::map<std::string, CgiSpec>  cgi_by_ext;

	// --- Rate limit (single declaration!) ---
	RateLimitConfig                 rate_limit;  // configured per-location

	PutPatchConfig                  write_conf;

	// new (parsing support)
	std::vector<std::string>        try_files;      // tokens until ';'
	int                             return_status;  // 0 if not set
	std::string                     return_target;  // optional url/text
	std::vector<std::string>        allow_list;     // CIDR or "all"
	std::vector<std::string>        deny_list;      // CIDR or "all"

	// uploads
	std::string                     upload_store;          // e.g. "/var/www/uploads"
	bool                            upload_overwrite;      // default false
	std::size_t                     upload_max_file_size;  // 0 = unlimited per part

	// proxy timeouts (if you added these)
	int                             proxy_connect_timeout_ms;
	int                             proxy_io_idle_timeout_ms;

	Location();
	~Location();
};


// ---------- VirtualServer ----------
class VirtualServer {
	public:
		std::string                      listen_host;
		int                              listen_port;
		std::vector<std::string>         server_names;
		std::string                      root;
		std::vector<std::string>         index_files;
		std::map<int, std::string>       error_pages;
		std::map<std::string, UpstreamPool> upstreams; // <- required by tests
		std::vector<Location>            locations;
		// new (server level)
		std::string                      client_body_temp_path;
		int                              client_max_body_size; // bytes, 0 = unlimited

		VirtualServer();
		~VirtualServer();
};

#endif // VIRTUALSERVER_H


