/* --- PutPatchHandler.cpp --- */

/* ------------------------------------------
author: undefined
date: 8/16/2025
------------------------------------------ */

#include "PutPatchHandler.h"

PutPatchHandler::PutPatchHandler()
{
	// Constructor
}

PutPatchHandler::~PutPatchHandler()
{
	// Destructor
}

/**
 * @brief Opens File with mode as a stream
 * 
 * Differs flags based on modes
 * @param to ofstream to open the file in
 * @param path C string of the file name
 * @param mode Mode in which to open the file
 * @note by default opens in PUT mode
 */
void openWithMode(std::fstream &to, const char *path, enum FileOpenMode mode)
{
	switch (mode)
	{
		case (PUT):
			return(to.open(path, std::ios::binary | std::ios::out | std::ios::trunc));
		case (APPEND):
			return(to.open(path, std::ios::binary | std::ios::out | std::ios_base::app));
		case (OVERWRITE):
			return(to.open(path, std::ios::binary | std::ios::in | std::ios::out));
		default:
			return(to.open(path, std::ios::binary |std::ios::out | std::ios::trunc));
	}

}

/**
 * @brief Writes into path file based off req body
 * 
 * Writes HttpRequest->body into a file created by
 * opening the HttpRequest->path with the specified mode
 * Skips past offset bytes, when the mode is bigger then APPEND or PUT
 * Writes in one chunk and performs test if opening and writing
 * was validly performed
 * @param path Name of the file to create and write
 * @param offset Offset after which the write begins
 * @param mode mode in which the file is to be opened
 * @param cont Vector of characters containing the content to write into a file
 */
int	writeFilePath(const char *path, std::size_t offset, enum FileOpenMode mode, const std::vector<char> &cont)
{
	std::fstream to; 
	std::size_t fileLength;

	// Try to open target
	openWithMode(to, path, mode);
	std::cout << "Opened file" << std::endl;

	// For Patch Methods, if file doesnt exist, its an error, for Put its not
	if (access(path, F_OK) != 0 && mode > PUT)
		return (HTTP_NOT_FOUND);

	// If still not open, then invalid permissions
	if (!to.is_open())
		return (HTTP_FORBIDDEN);
	std::cout << "File stream was opened" << std::endl;

	// Skip to offset when required
	if (mode > APPEND)
	{
		to.seekg(0, std::ios::end);
		fileLength = to.tellg();
		if (offset <= fileLength)
			to.seekp(offset, std::ios::beg);
		else
			to.seekp(fileLength, std::ios::beg);
	}

	std::cout << "Skipped offset if specified" << std::endl;

	// Extra check in case seeking caused error
	if (!to.good())
	{
		to.close();
		return (HTTP_INV_SERVER_ERR);
	}
	std::cout << "Stream still good" << std::endl;

	// Write in a single write from our <cont> into our target
	to.write(cont.data(), cont.size());

	std::cout << "Wrote data to file" << std::endl;
	// If error in target
	if (!to.good())
	{
		to.close();
		return (HTTP_INV_SERVER_ERR);
	}
	to.close();
	return (0);
}

/**
 * @brief Writes into path file based off temp file
 * 
 * Writes Temporary File containing body into a file 
 * created by opening the HttpRequest->path with the specified mode
 * Skips past offset bytes, when the mode is bigger then APPEND or PUT
 * Writes in one chunk and performs test if opening and writing
 * was validly performed
 * @param path Name of the file to create and write
 * @param offset Offset after which the write begins
 * @param mode mode in which the file is to be opened
 * @param source Name of the temporary file holding the body information
 */
int	writeFileTemp(const char *path, std::size_t offset, enum FileOpenMode mode, const std::string &source)
{
	std::fstream to; 
	std::ifstream from;
	char buffer[PUT_WRITE_BUFFER_SIZE];

	// Try to open target with mode
	openWithMode(to, path, mode);

	// For Patch Methods, if file doesnt exist, its an error, for Put its not
	if (access(path, F_OK) != 0 && mode > PUT)
		return (HTTP_NOT_FOUND);

	// If still not open, then invalid permissions
	if (!to.is_open())
		return (HTTP_FORBIDDEN);

	// Try to open source (hehe)
	from.open(source.c_str(), std::ios::in);
	if (!from.is_open())
	{
		to.close();
		return (HTTP_FORBIDDEN);
	}

	// Skip to offset when required
	if (mode > APPEND)
	{
		to.seekg(0, std::ios::end);
		std::size_t fileLength = to.tellg();
		if (offset <= fileLength)
			to.seekp(offset, std::ios::beg);
		else
			to.seekp(fileLength, std::ios::beg);
	}

	// Extra check in case seeking caused error
	if (!to.good())
	{
		to.close();
		return (HTTP_INV_SERVER_ERR);
	}

	// While the <from> streams badbit and failbit are both set (stream still has data)
	while (from)
	{
		// Read data into buffer, so we can see how much was written
		from.read(buffer, PUT_WRITE_BUFFER_SIZE);
		std::streamsize written = from.gcount();

		// Write as much into <to> as was read from <from>
		if (written > 0)
			to.write(buffer, written);
		
		// If error in <to> fstream
		if (!to.good())
		{
			from.close();
			to.close();
			return (HTTP_INV_SERVER_ERR);
		}
	}
	from.close();
	to.close();
	return (0);
}

/**
 * @brief Choose the write style for the request
 * 
 * Decide wether file should be written with input from another file or from body of request
 * Decides based on Request Context
 * @param path C String to the file to be written to
 * @param offset Amount of bytes to skip before writing
 * @param mode mode in which the file should be opened
 * @param req Http Request, which holds the body for writing
 * @param ctx Context of the Request, which says which type of write we use
 */
int decideWrite(const char *path, int offset, enum FileOpenMode mode, HttpRequest &req, RequestContext &ctx)
{
	if (ctx.temp_file_used)
	{
		std::cout << "Decided to use Temp File for content" << std::endl;
		return (writeFileTemp(path, offset, mode, ctx.temp_filename));
	}
	else
	{
		std::cout << "Decided to use body for content" << std::endl;
		return (writeFilePath(path, offset, mode, req.getBody()));
	}
}

/**
 * @brief Creates or overwrites a file
 * 
 * Checks if file exists, then tries to write to file
 * Writing is based on if the context (ctx) temp_file_used is set to true
 * If true, then the file gets written based on a teporary files data
 * If false, then the file gets written based on the Requests entire body
 * Returns different codes based on if file already existed and was modified
 * or if the file was created completely new
 * @param req Request that specifies the file to overwrite
 * @param res Response, which should be prepared by this function (UNUSED)
 * @param ctx Context, how the file should be created
 * @returns HTTP_FILE_CREATED if new resource created 
 * @returns HTTP_OK if existing file updated
 * @returns HTTP_FORBIDDEN if no write permissions on this file
 * @returns HTTP_INV_SERVER_ERR on unexpected errors
 */
int	PutPatchHandler::handle_put(HttpRequest &req, HttpResponse &res, RequestContext &ctx)
{
	int			status;
	bool		exists;
	std::string path_string;
	
	(void)res;
	// Make path for file to put
	path_string = req.getPath();
	const char *path = path_string.c_str();

	std::cout << "Target path is [" << path << "]" << std::endl;
	// Store if the file is new or not, for later
	exists = access(path, F_OK) + 1;
	std::cout << "Target file exists: " << exists << std::endl;

	// Create file and write to it
	status = decideWrite(path, 0, PUT, req, ctx);

	// If Any exit code encountered in writing, exit with that
	if (status != 0)
		return (status);
	
	// If File was created from fresh -> 201, otherwise -> 200
	if (exists)
		return (HTTP_OK);
	return (HTTP_FILE_CREATED);
}

/**
 * @brief Populates the given params based on the given input
 * 
 * It is mandatory to have A [TYPE], '/', [SUBTYPE]
 * After that, the input may end, or continue on like:
 * ';', SPACES, [PARAM]
 * Errors include:
 * No '\'
 * No Type
 * No subtype
 * (if parameters: No empty params)
 * @returns false if error encountered
 */
bool	populatePatchMethod(const std::string &input, std::string &type, \
							std::string &subtype, std::string parameter)
{
	size_t	start = 0;
	size_t	end = 0;

	// Get type, before '\'
	end = input.find('/');
	if (end == std::string::npos)
		return (false);
	type = input.substr(start, end - start);
	if (type.empty())
		return (false);
	start = end + 1;
	
	// Subtype may be followed by ; or not
	end = input.find(';');
	if (end == std::string::npos)
	{
		subtype = input.substr(start, input.length() - start);
		if (subtype.empty())
			return (false);
		return (true);
	}
	// If ; after subtype
	subtype = input.substr(start, end - start);
	if (subtype.empty())
		return (false);
	start = end + 1;
	while (input[start] == ' ')
		start++;

	parameter = input.substr(start, input.length() - start);
	if (parameter.empty())
		return (false);
	return (true);
}

/**
 * @brief Checks if specified path method is allowed in config and sets mime-type
 * 
 * Iterates over all elements of the MIME types stored in server config and checks
 * if the chosen_mime matches their value.
 * If so, sets the mime_type to this key value and returns true
 * @param chosen_mime Combination of MIME-type '/' MIME-subtype, is searched in map
 * @param mime_type Will be set by function to short-hand for the chosen mime type, if found in map
 * @param mime_map Map of all MIME Type/subtype combos known to the Server Configuration
 * @returns true if chosen_mime was found
 * @returns false if chosen_mime is not allowed
 */
bool allowedPatchMethod(const std::string &chosen_mime, std::string &mime_type, std::map<std::string, std::string> mime_map)
{
	std::map<std::string, std::string>::const_iterator it;

	// For each Mime Map Entry
	for (it = mime_map.begin(); it != mime_map.end(); it++)
	{
		if (it->second == chosen_mime)
		{
			mime_type = it->first;
			return (true);
		}
	}
	return (false);
}

/**
 * @brief Append Patch Method
 * 
 * Writes given information at the end of the target
 * [OLD] [NEW]
 * @param req Request specifying which file and what content
 * @param ctx Context about Request
 * @returns Http Exit Code
 */
int	applyPatchAppend(HttpRequest &req, RequestContext &ctx)
{
	return (decideWrite(req.getPath().c_str(), 0, APPEND, req, ctx));
}

/**
 * @brief Overwrite Patch Method
 * 
 * Writes given information at a specific offset
 * Keeps old data before that, overwrites for each character in data
 * Tries to conserve data after offset
 * Meaning that if offset = 2, data_len = 4, oldLen = 10
 * We turn
 * [10 OLD] => [2 OLD] [4 NEW] [4 OLD]
 * @param req Request specifying which file and what content
 * @param ctx Context about Request
 * @returns Http Exit Code
 */
int	applyPatchOverwrite(size_t offset, HttpRequest &req, RequestContext &ctx)
{
	return (decideWrite(req.getPath().c_str(), offset, OVERWRITE, req, ctx));
}

/**
 * @brief Checks which patching method is asked for and calls it
 * @param type Type of patch method
 * @param param Parameters of this type
 * @param req Request that specifies the file to overwrite
 * @param res Response, which should be prepared
 * @param ctx Context, how the file should be created
 */
int	applyPatch(const std::string &type, const std::string &param, HttpRequest &req, RequestContext &ctx)
{
	std::string types[2] = {"p_append", "p_overwrite"};
	size_t	offset;
	bool	offset_error;
	int i = 0;

	// unused
	(void)param;

	// Get offset for patching (if error is encountered, keep going with 0 value)
	offset = Atoi::atoiPatchOffset(req.getHeaders().get(HDR_PATCH_OFFSET).c_str(), offset_error);

	// Get Case we have
	for (; i < 2; i++)
	{
		if (types[i] == type)
			break ;
	}

	// Execute Patch method
	switch (i)
	{
		case 0:
			return (applyPatchAppend(req, ctx));
		case 1:
			return (applyPatchOverwrite(offset, req, ctx));
		default:
			return (HTTP_INV_MEDIA);
	}
}

/**
 * @brief Set the Response Headers HDR_ACCEPT_PATCH field
 * @param res HttpResponse, whoose fields to set
 * @param mime_map Map of all allowed MIME types in the server config
 * 
 * Iterates through all Mime mappings in the ServerConfig,
 * checks if they are a registered Patch method,
 * then stores them into a string and sets the Header field to that string
 */
void	responseSetAllowed(HttpResponse &res, const std::map<std::string, std::string> &mime_map)
{
	std::map<std::string, std::string>::const_iterator it_mime;
	std::string types[2] = {"application/vnd.webserv.append", "application/vnd.webserv.overwrite"};
	std::vector<std::string> allowed_list;
	std::string allowed;

	// For each Mime Map Entry
	for (it_mime = mime_map.begin(); it_mime != mime_map.end(); it_mime++)
	{
		// Store it, if we found an allowed Patch Mime Type
		if (it_mime->second == types[0] || it_mime->second == types[1])
			allowed_list.push_back(it_mime->second);
	}

	// For all the registered allowed methods, make them into a string
	for (std::vector<std::string>::const_iterator it_alow = allowed_list.begin(); \
	it_alow != allowed_list.end(); it_alow++)
	{
		if (it_alow != allowed_list.begin())
			allowed += ", ";
		allowed += *it_alow;
	}
	res.headers.set(HDR_ACCEPT_PATCH, allowed);
}

/**
 * @brief Handles a Patch Request (augmenting an existing file)
 * @param req Request that specifies the file to overwrite
 * @param res Response, which should be prepared by this function
 * @param ctx Context, how the file should be created
 * @returns	HTTP_INV_MEDIA (415) if no valid Patch Method was specified in Header
 * @returns	HTTP_BAD_REQUEST (400) if Path Method is invalidly configured
 * @returns HTTP_NOT_FOUND (404) if target file is not existant
 * @returns HTTP_FORBIDDEN (403) if target file cannot be opened, but exists
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
int	PutPatchHandler::handle_patch(HttpRequest &req, HttpResponse &res, RequestContext &ctx)
{
	std::string chosen_mime;
	std::string type;
	std::string subtype;
	std::string parameters;
	std::string short_type;

	// Set Allowed MIME types for patching in response
	responseSetAllowed(res, ctx.cfg->mime_mapping);

	// Should have methods specified on how to patch
	if (!req.getHeaders().keyExists(HDR_CONTENT_TYPE))
		return (HTTP_INV_MEDIA);

	// Get patch methods
	chosen_mime = req.getHeaders().get(HDR_CONTENT_TYPE);
	if (!populatePatchMethod(chosen_mime, type, subtype, parameters))
		return (HTTP_BAD_REQUEST);
	
	// Check if specified Mime type is allowed in config
	if (!allowedPatchMethod((CSTM_PATCH + type + "/" + subtype), short_type, ctx.cfg->mime_mapping))
		return (HTTP_INV_MEDIA);

	// Apply whatever method was specified to the file
	return (applyPatch(short_type, parameters, req, ctx));
}

// Patch Notes
/**
 * If the entire patch document
   cannot be successfully applied, then the server MUST NOT apply any of
   the changes.

   Accept-Patch response header (says which methods for patching are okay)
   Should be given in the OPTIONS field of response for file that accepts PATCH method

   Header Fiels:
   Accept-Patch = "Accept-Patch" ":" 1#media-type
   example:		Accept-Patch: application/example, text/example
				Accept-Patch: text/example;charset=utf-8

   Malformed patch document -> 400
   Unsupported patch document -> 415
   Unprocessable request -> 422 (Document type and format are valid, but server cant do request)
   Resource not found -> 404 (file not found)
   Conflicting state -> 409 (Structures assumed to exist, dont)
   Conflicting modification -> 412 (Preconditions for Patching failed)

 */