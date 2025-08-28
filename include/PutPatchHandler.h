/* --- PutPatchHandler.h --- */

/* ------------------------------------------
Author: undefined
Date: 8/16/2025
------------------------------------------ */

#ifndef PUTPATCHHANDLER_H
# define PUTPATCHHANDLER_H

# include <fcntl.h>
# include <unistd.h>
# include <iostream>
# include <fstream>
# include <vector>
# include "Handler.h"
# include "HTTPCODES.h"
# include "HEADER_ENTRIES.h"
# include "Atoi.h"

// Buffer size and how many bytes are read when we read from temp file containing body
# define PUT_WRITE_BUFFER_SIZE 8192
// Prefix for our custom patch methods (vnd.webserv.insert)
# define CSTM_PATCH "vnd.webserv."
// Mime Type for Patching in append mod
# define MIME_PATCH_APPEND "application/vnd.webserv.append"
// Mime Type for Patching in overwriting mod
# define MIME_PATCH_OVERWRITE "application/vnd.webserv.overwrite"
// Counter how many Patch methods we have programmed
# define MIME_PATCH_COUNTER 2

// What type our given Request is (used for writing, to distinguish behaviour)
enum FileOpenMode
{
	PUT,
	APPEND,
	OVERWRITE
};

/**
 * Handles the PUT and PATCH methods
 * Theese try to write to the target file as specified in the HttpRequest
 * The Content to be written will be its body, either in the Request or in a seperate file
 * The amount of bytes written depends on the Requests HDR_CONTENT_LENGTH
 * See the respective functions for further information
 * @warning Normally, patching only changes a file, if all operations succeed
 * But right now, if something fails midway, the file will still be halfway changed
 * As we do not have methods that are prone to failing midway (like a json patch),
 * i have elected to skip this feature for now
 * @warning Neither of theese functions checks, if the User is allowed to use them,
 * as it seemed easier for the caller to check this, in line with the other handlers
 * @note Adding a new patch method:
 * 1) Add a new Header Definition
 * 		#define MIME_PATCH_<new-type>
 * 2) Update MIME_PATCH_COUNTER by 1
 * 3) Add your new MIME_PATCH_<new-type> in responseSetAllowed() in the types variable
 * 4) Add it as part of the above enum FileOpenMode
 * 		(Should be added at the bottom of the enum)
 * 5) Add an if statement for calling it in handle_patch() as well as a function
 * 		(Try and orient yourself on the other patch functions like applyPatchAppend())
 * Relevant Headers:
 * Put: Response: HDR_CONTENT_TYPE
 * Patch: Response: HDR_CONTENT_TYPE, HDR_PATCH_OFFSET  Request: HDR_ACCEPT_PATCH
 */
class PutPatchHandler : public Handler
{
	public:
		PutPatchHandler();
		~PutPatchHandler();
		
		/**
		 * @brief Creates or overwrites a file
		 * @param req Request that specifies the file to overwrite
		 * @param res UNUSED Http Response, needs to be here to match the (...)::handle() prototype
		 * @param ctx Context, how the file should be created
		 * @returns HTTP_FILE_CREATED (201) if new resource created 
		 * @returns HTTP_OK (200) if existing file updated
		 * @returns HTTP_FORBIDDEN (403) if no write permissions on this file
		 * @returns HTTP_INV_SERVER_ERR (500) on unexpected errors
		 * 
		 * Checks if file exists, then tries to write to file
		 * Writing is based on if the context (ctx) temp_file_used is set to true
		 * If true, then the file gets written based on a teporary files data
		 * If false, then the file gets written based on the Requests entire body
		 * Returns different codes based on if file already existed and was modified
		 * or if the file was created completely new
		 */
		static int	handle_put(HttpRequest &req, HttpResponse &res, RequestContext &ctx);

		/**
		 * @brief Handles a Patch Request (augmenting an existing file)
		 * @param req Request that specifies the file to overwrite
		 * @param res Response, which will get some Header Data set from this function
		 * @param ctx Context, how the file should be created
		 * @returns	HTTP_BAD_REQUEST (400) if Path Method is invalidly configured
		 * @returns HTTP_FORBIDDEN (403) if target file cannot be opened, but exists
		 * @returns HTTP_NOT_FOUND (404) if target file is not existant
		 * @returns	HTTP_INV_MEDIA (415) if no valid Patch Method was specified in Header
		 * @returns HTTP_INV_SERVER_ERR (500) if error while writing to file
		 * 
		 * Each Patch Requests must come with a specified method for patching data
		 * This is specified in the Header 
		 * (HDR_CONTENT_TYPE) Content-Type: application/vnd.webserv.<type>
		 * where <type> can be "append" or "overwrite"
		 * This method has to be a ServerConfig-allowed MIME-type
		 * There should also be a Header for an offset, which is used in specific methods like overwrite
		 * (HDR_PATCH_OFFSET) Parse-Offset: <number>
		 * where <number> is the amount of bytes after which we write
		 * 
		 * The Patching will set an Http Exit Codein the response
		 * It will also set a Header in the response:
		 * (HDR_ACCEPT_PATCH) Accept-Patch: application/vnd.webserv.<type>, ...
		 * Where it will give back all allowed patch MIME types in the specified format,
		 * in a comma seperated string
		 */
		static int	handle_patch(HttpRequest &req, HttpResponse &res, RequestContext &ctx);
};

#endif // PUTPATCHHANDLER_H
