#include "VirtualServer.h"

// ---- Upstream / UpstreamPool ----
Upstream::Upstream()
    : host()
    , port(0)
    , healthy(true)
    , weight(1)
{}
Upstream::~Upstream() {}

UpstreamPool::UpstreamPool()
    : nodes()
    , strategy("roundrobin")
    , health_path()
    , health_interval_ms(0)
{}
UpstreamPool::~UpstreamPool() {}

// ---- VirtualServer ----
VirtualServer::VirtualServer()
: listen_host(), listen_port(0), server_names(), root(),
  index_files(), error_pages(), upstreams(), locations(),
  client_body_temp_path(), client_max_body_size(0) {}

VirtualServer::~VirtualServer() {}


