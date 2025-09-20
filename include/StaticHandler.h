/* --- StaticHandler.h --- */

/* ------------------------------------------
Author: undefined
Date: 8/10/2025
------------------------------------------ */

#ifndef STATICHANDLER_H
#define STATICHANDLER_H

#include "Handler.h"
#include "RequestContext.h"
#include "VirtualServer.h"
#include "ServerConfig.h"
#include "HttpRequest.h"
#include "HttpResponse.h"
#include "Headers.h"
#include "HEADER_ENTRIES.h"
#include "HttpPreconditions.h"
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <cstdio>
#include <sstream>
#include <vector>
#include <string>
#include <algorithm>
#include <map>
#include <limits.h> // PATH_MAX

// In Case the StaticHandler wants to give back an Error Page, 
// but cant find the specified Error Page File, it returns this as Response
#define FALLBACK_404 "<!DOCTYPE html>\n<html><head><title>404 Not Found</title></head>\n<body><h1>Fallback 404: Not Found</h1><p>The requested resource was not found.</p></body></html>"

class StaticHandler : public Handler
{
	public:
		StaticHandler();
		~StaticHandler();
		bool	handleGet(const std::string &canonPath, const std::string &rel, \
				bool is_head, HttpRequest &req, HttpResponse &res, RequestContext &ctx);

		/**
		 * @brief Deletes the target file
		 * @param path Path to the file that should be deleted
		 * @param res Response to set
		 * @param ctx Context for Request
		 * @returns true if Succes
		 * @returns false if Failure
		 * @note Handled Error Codes: 404, 200, 403
		 * @note In theory, one should check the parent directories permissions
		 * to check if deleting is valid
		 * @note Currently struggles with sending back error pages
		 * Because the error page setup uses effective_root + target
		 * And for DELETE the effective_root will be something like
		 * /home/.../www/upload/dir
		 * And the current version of the error page getter would look for
		 * effective_root + "/errors/40x.html"
		 * /home/.../www/upload/dir/errors/40x.html
		 * However, the errors directory is not in the upload/dir folder
		 * So we will need to change the error file searching behaviour
		 */
		bool handleDelete(const std::string &path, HttpResponse res, RequestContext ctx);

		/**
		 * @brief Handler for accesing Static resources
		 * 
		 * Handles DELETE, GET and HEAD methods
		 * Used Codes: 200, 403, 404, 500
		 * @param req Http Request to process
		 * @param res Http Response to prepare
		 * @param ctx Request Context
		 * @returns bool, wether Processing was success
		 */
		bool handle(HttpRequest &req, HttpResponse &res, RequestContext &ctx);

	private:
};

#endif // STATICHANDLER_H
