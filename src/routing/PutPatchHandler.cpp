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
	bool exists;

	// Store if file existed alredy, for later
	exists = access(path, F_OK) + 1;

	// Try to open target
	openWithMode(to, path, mode);

	// For Patch Methods, if file doesnt exist, its an error, for Put its not
	if (access(path, F_OK) != 0 && mode > PUT)
		return (HTTP_NOT_FOUND);

	// If still not open, then invalid permissions
	if (!to.is_open())
		return (HTTP_FORBIDDEN);

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

	// Extra check in case seeking caused error
	if (!to.good())
	{
		to.close();
		return (HTTP_INV_SERVER_ERR);
	}

	// Write in a single write from our <cont> into our target
	to.write(cont.data(), cont.size());

	// If error in target
	if (!to.good())
	{
		to.close();
		return (HTTP_INV_SERVER_ERR);
	}
	to.close();
	if (exists)
		return (HTTP_OK);
	return (HTTP_FILE_CREATED);
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
	bool exists;

	// Store if file existed alredy, for later
	exists = access(path, F_OK) + 1;
	
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
	if (exists)
		return (HTTP_OK);
	return (HTTP_FILE_CREATED);
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
		return (writeFileTemp(path, offset, mode, ctx.temp_filename));
	else
		return (writeFilePath(path, offset, mode, req.getBody()));
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
	std::string types[MIME_PATCH_COUNTER] = {MIME_PATCH_APPEND, MIME_PATCH_OVERWRITE};
	std::vector<std::string> allowed_list;
	std::string allowed;

	// For each Mime Map Entry
	for (it_mime = mime_map.begin(); it_mime != mime_map.end(); it_mime++)
	{
		// Check against all programmed MIME types for Patch
		for (int i = 0; i < MIME_PATCH_COUNTER; i++)
		{
			// If a programmed one was found, add it to the list
			if (it_mime->second == types[i])
			{
				allowed_list.push_back(it_mime->second);
				break;
			}
		}
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
 * @brief Checks if specified path method is allowed in config of MIME types
 * 
 * Iterates over all elements of the MIME types stored in server config and checks
 * if the stripped input matches their value.
 * @param input Given Mime-Type, with or without parameters, will be checked without them
 * @param mime_map Map of all MIME Type/subtype combos known to the Server Configuration
 * @returns (HTTP_BAD_REQUEST) 400 if MIME-Type missing
 * @returns 0 if given MIME-Type was found
 * @returns (HTTP_INV_MEDIA) 415 if MIME-Type is not registered in Server Config 
 */
int allowedPatchMethod(const std::string &input, std::map<std::string, std::string> mime_map)
{
	std::map<std::string, std::string>::const_iterator it;
	size_t	end = 0;
	std::string type;
	
	// Strip Mime Type of Parameters
	end = input.find(';');
	type = input;
	if (end != std::string::npos)
		type = input.substr(0, end);
	if (type.empty())
		return (HTTP_BAD_REQUEST);

	// For each Mime Map Entry
	for (it = mime_map.begin(); it != mime_map.end(); it++)
	{
		if (it->second == type)
			return (0);
	}
	return (HTTP_INV_MEDIA);
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

int	PutPatchHandler::handle_put(HttpRequest &req, HttpResponse &res, RequestContext &ctx)
{
	(void)res;
	return (decideWrite(req.getPath().c_str(), 0, PUT, req, ctx));
}

int	PutPatchHandler::handle_patch(HttpRequest &req, HttpResponse &res, RequestContext &ctx)
{
	std::string	type;
	int			status;
	size_t		offset;

	// Set Allowed MIME types for patching in response
	responseSetAllowed(res, ctx.cfg->mime_mapping);
	
	// Should have methods specified on how to patch
	if (!req.getHeaders().keyExists(HDR_CONTENT_TYPE))
		return (HTTP_INV_MEDIA);
	
	// Check if specified Mime type is valid and allowed in config
	type = req.getHeaders().get(HDR_CONTENT_TYPE);
	status = allowedPatchMethod(type, ctx.cfg->mime_mapping);
	if (status)
		return (status);
	
	// Get offset for patching (if error is encountered, keep going with 0 value)
	offset = Atoi::atoiPatchOffset(req.getHeaders().get(HDR_PATCH_OFFSET).c_str());

	// Apply that method
	if (type == MIME_PATCH_APPEND)
		return (applyPatchAppend(req, ctx));
	else if (type == MIME_PATCH_OVERWRITE)
		return (applyPatchOverwrite(offset, req, ctx));
	return (HTTP_INV_MEDIA);
}
