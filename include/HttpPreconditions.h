#ifndef HTTP_PRECONDITIONS_H
#define HTTP_PRECONDITIONS_H

#include "ETagUtil.h"
#include "HttpRequest.h"
#include "Headers.h"
#include "HEADER_ENTRIES.h"
#include <string>
#include <ctime>
#include <vector>

class HttpRequest;

/**
 * Minimal HTTP preconditions helper (ETag + Last-Modified)
 *
 * Implements:
 *  - If-None-Match: exact match against a list of ETags (or "*")
 *  - If-Modified-Since: RFC1123 date; true if mtime <= IMS
 *  - If-Match: exact match against a list of ETags (or "*")
 *
 * Interpretation:
 *  - A false return means that the Precondition has failed
 * 	  A failed precondition usually indicates that the resouce has no changes, so -> 304
 *  - The program should run normally, if all Preconditions are true
 */
namespace HttpPreconditions
{
	/**
	 * @brief Checks if the specified ETag matches the one in the request Header
	 * @param req HttpRequest which holds Header fields to check
	 * @param etag String that should be a newly created ETag for the target file
	 * @returns true, if Condition is satisfied (Matching ETag)
	 * @returns false, if Condition violated (if ETag could not be found)
	 * @note Should only handle strong ETags (no W/"blablabla")
	 * @note https://www.rfc-editor.org/rfc/rfc9110.html#section-13.1.1
	 */
	bool	checkIfMatch(const HttpRequest &req, const std::string &etag);

	/**
	 * @brief Checks the If-None-Match Header
	 * @param inm Value of the Header Field for If-None-Match
	 * @param etag Current ETag
	 * @returns true, if Condition is satisfied (Not Matching ETag)
	 * @returns false, if Condition violated (if ETag could be found)
	 * @note Will handle weak ETags (W/"123" == "123")
	 * @note https://www.rfc-editor.org/rfc/rfc9110.html#section-13.1.2
	 */
	bool	checkIfNoneMatch(const HttpRequest &req, const std::string &etag);

	/**
	 * @brief Checks the If-Modified-Since Header Condition
	 * @param req Request that holds the Headers to check
	 * @param mtime Time since when the file was modified (stat struct st_mtime field)
	 * @returns true if Condition is true (No such Header, or modification time is less then specified)
	 * @returns false if Condition violated
	 * @note https://www.rfc-editor.org/rfc/rfc9110.html#section-13.1.3
	 */
	bool	checkIsModifiedSince(const HttpRequest& req, std::time_t mtime);

	/**
	 * @brief Precondition check for GET method
	 * @param req HttpRequest with Headers containing precondition fields
	 * @param etag ETag for the current file to access
	 * @param mtime Time since when the current file was last modified
	 * @returns true if all preconditions are met
	 * @returns false if a precondition fails (meaning the file hasnt changed -> 304)
	 */
	bool	getPreconditons(const HttpRequest &req, const std::string &etag, const std::time_t &mtime);

	/**
	 * @brief Precondition check for PUT and PATCH
	 * @param req HttpRequest with Headers containing precondition fields
	 * @param etag ETag for the current file to access
	 * @returns true if all preconditions are met
	 * @returns false if a precondition fails (meaning the file hasnt changed -> 412)
	 */
	bool	putpatchPreconditons(const HttpRequest &req, const std::string &etag);
}

#endif // HTTP_PRECONDITIONS_H
