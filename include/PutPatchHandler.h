/* --- PutPatchHandler.h --- */

/* ------------------------------------------
Author: undefined
Date: 8/16/2025
------------------------------------------ */

#ifndef PUTPATCHHANDLER_H
#define PUTPATCHHANDLER_H

#include <fcntl.h>
#include <unistd.h>
#include <iostream>
#include <fstream>
#include <vector>
#include "Handler.h"
#include "HTTPCODES.h"
#include "HEADER_ENTRIES.h"
#include "Atoi.h"

// Buffer size and how many bytes are read when we read from temp file containing body
#define PUT_WRITE_BUFFER_SIZE 8192
// Prefix for our custom patch methods (vnd.webserv.insert)
#define CSTM_PATCH "vnd.webserv."

enum FileOpenMode
{
	PUT,
	APPEND,
	OVERWRITE
};

/**
 * PUT replaces a source entirely
 * PATCH modifies an existing source
 * So writing 1, 2 or 3 PUTS always results in the same result
 * Writing patch multiple times could increase for example, a counter, resulting in differing results
 * 
 * May have a body
 * 
 * path of Request specifies target file when combined with Host
 * 
 * The body of the PATCH method specifies the data to override
 */

class PutPatchHandler : public Handler
{
	public:
		PutPatchHandler();
		~PutPatchHandler();
		static int	handle_put(HttpRequest &req, HttpResponse &res, RequestContext &ctx);
		static int	handle_patch(HttpRequest &req, HttpResponse &res, RequestContext &ctx);
	private:
};

#endif // PUTPATCHHANDLER_H
