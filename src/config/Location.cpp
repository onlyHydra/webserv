/* --- Location.cpp --- */

/* ------------------------------------------
author: undefined
date: 8/10/2025
------------------------------------------ */

#include "VirtualServer.h"

Location::Location()
: path_prefix(), regex(false), root(), autoindex(false),
  index_files(), allowed_methods(), upload_dir(), is_proxy(false),
  proxy_name(), cgi_by_ext(), rate_limit(), write_conf(),
  try_files(), return_status(0), return_target(), allow_list(), deny_list() {}

Location::~Location() {}





