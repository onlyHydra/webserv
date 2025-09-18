/* --- HttpRequest.h --- */

/* ------------------------------------------
Author: undefined
Date: 8/10/2025
------------------------------------------ */

#ifndef HTTPREQUEST_H
 #define HTTPREQUEST_H

# include <string>
# include <vector>
# include <errno.h>
# include "Headers.h"
# include "CookieJar.h"
# include "Atoi.h"
# include "HTTPCODES.h"

using namespace std;

/**
 * For HttpRequest->state
 * Represents in which stage / state of parsing it is
 */
enum HttpRequestState
{
	ERROR,
	START,
	HEADER,
	NEWLINE,
	BODY,
	OVER
};

/**
 * @brief Class that represents a full Http Request
 * ---------------------------------------------------
 * Has a Starting Line that looks like
 * <method> <path> <http_version>
 * then has a Header, consisting of many key-value pairs
 * <key1>: <value>
 * <key2>: <value>
 * ....
 * And may then have a body, based on the method
 * ---------------------------------------------------
 * Use the parse() member function to feed it input to parse
 * Use getState to see in which state the Request is (see HttpRequestState)
 * Use the getter functions to get the fields set by the parser
 * ---------------------------------------------------
 * For the parsing, choose to either ALWAYS remove handled data from buffer
 * or not at all, then use the parsing like this:
 * 1)	Add new content to your callers buffer
 * 2)	Calculate the parseOffset with:
 * 			If you dont free handled data from your callers buffer
 * 				parseOffset = totalBytesRead
 * 			else if you ALWAYS free handled data from your callers buffer
 * 				parseOffset = totalBytesRead - totalBytesHandled
 * 3)	Give your buffer indexed with parseOffset and the size of the rest into parse()
 * 			parse(&(buffer[parseOffset]), buffer.size() - parseOffset)
 * 4)	If return is false 
 * 			Error out in case parse returns false (will also set errno)
 * 		else if return is true
 * 			Remove handled data from your caller buffer, or not
 * 			buffer = buffer.substr(x.getBytesHandledLast());
 * ---------------------------------------------------
 * Sets errno to ERR_HTTP_BAD_REQUEST on bad input
 * Sets errno to ERR_HTTP_HEADER_LIMIT when too many Header fields are given
 * ---------------------------------------------------
 * Points of failure:
 * Does not handle the query field, session_id and uri so far
 */
class HttpRequest
{
	private:
		string method;
		string path;
		string http_version;
		string uri;
		string query;
		string session_id;
		size_t	bodyLength;
		Headers headers;
		vector<char> body;
		

		/* If body is large we can offload it to a temp file. When true, the
		   file at body_tmp_path contains the request body and `body` vector is
		   kept empty. Use the API helpers below to manipulate the on-disk body.
		*/
		bool	body_on_disk;
		std::string body_tmp_path;
		// bytes written into the on-disk body file so far
		size_t	body_on_disk_bytes;
		string buffer;
		size_t	totalBytesRead;
		size_t	totalBytesHandled;
		size_t	bytesHandledLast;
		bool conType;
		enum HttpRequestState state;

		/**
		 * @name handleLineStart
		 * @brief DONT USE DIRECTLY | Parses a given line as a valid Starting Line
		 * @param in Line to be handled
		 * @returns 0 if Operation worked
		 * @returns HTTP Error Codes if Parsing failed
		 * Error checks:
		 * - Space after Method (400)
		 * - Valid Method (400)
		 * - Space after Path (400)
		 * - Valid Path (400)
		 * - \r\n after Http Version (400)
		 * - Valid Http Version (400)
		 * - Ending after \r\n (400)
		 */
		int	handleLineStart(const std::string &in);

		/**
		 * @name handleLineHeader
		 * @brief DONT USE DIRECTLY | Parses a given line as Header Information
		 * @param in Line to be handled
		 * @returns 0 if Operation worked
		 * @returns HTTP Error Codes if Parsing failed
		 * Treats Content-Length key special, as it is required to have a body
		 * Error checks:
		 * - Colon after Key (400)
		 * - Valid Key (400)
		 * - \r\n after Value (400)
		 * - Valid Value (400)
		 * - Ending after \r\n (400)
		 * - Would exceed Header Size Limit (431)
		 */
		int	handleLineHeader(const std::string &in);

		/**
		 * @name handleLineBody
		 * @brief DONT USE DIRECTLY | Parses a given line as Body Information
		 * @param in Line to be handled
		 * @returns 0 if Operation worked
		 * @returns HTTP Error Codes if Parsing failed
		 * Takes each character from the passed line and puts it
		 * to the back of the body vector
		 * Error checks:
		 * - Only specific Methods may have a body (400)
		 * - Body may only exists, when Content-Length key exists in Header (400)
		 * - Body may not exceed the specified ContentLength (400)
		 */
		int	handleLineBody(const std::string &in);

		/**
		 * @name handleLine
		 * @brief DONT USE DIRECTLY | Gets a line from our buffer, and handles it
		 * @returns 0 if Operation worked
		 * @returns HTTP Error Codes if Parsing failed
		 * Executes different handlers based on the current state
		 * Error checks:
		 *  - Request is finished (400)
		 *  - Request ended in error (400, 431)
		 *  - Invalid state (400)
		 */
		int	handleLine(const std::string &in);

	public:
		CookieJar cookies;
		HttpRequest();

		~HttpRequest();

		/**
		 * @returns wether session is still alive
		 * For Later
		 * PLACEHOLDER
		 */
		bool	keepAlive(void) const;

		/**
		 * For Later
		 * PLACEHOLDER
		 */
		bool	headerAsSize(string k, size_t &v) const;

		/**
		 * @brief Adds data to the Requests Body
		 * @param data Body to append
		 * @param len Amount of characters to append
		 * @returns false, if state is ERROR, len is too small, or new length exceeds bodyLength
		 * @returns true if succesfull
		 */
		bool appendBody(const char* data, std::size_t len);
    	// const std::vector<char>& getBody() const;   // you likely already have this

		/**
		 * For Later
		 * PLACEHOLDER
		 */
		string	extension() const;

		/**
		 * ----------------------------------------------------------------------
		 * @brief Tell the Request to handle whatever is stored in its buffer
		 * @param activity will be set to true, if function worked
		 * @returns 0 if Operation worked
		 * @returns HTTP Error Codes if Parsing failed (400, 431)
		 * @warning Calling it will not do much, as the buffer will always be emptied as much as
		 * @warning possible after parse() is called. So dont do it.
		 * If not at Body Parsing, then the content can only be handled if a \r\n is present
		 * If at Body Parsing, whole buffer will be handled
		 */
		int	handleInput(bool &activity);

		/**
		 * Will save the given input into a buffer, handling it whenever possible
		 * Runs execution code based on the stage (state) it is in currently
		 * When state is Over or Error, does nothing
		 * ----------------------------------------------------------------------
		 * @brief Iterated Input Parser
		 * @param data C String containing information for the parser
		 * @param n the exact number of characters that data is big
		 * @returns true if it parsed correctly
		 * @returns false if error
		 * @warning param data ALWAYS has to be new information, as calls will chain information
		 * Error Checks:
		 * - Request is finished
		 * - Given length is 0 or less
		 * - Request has valid syntax for current stage
		 */
		bool	parse(const char* data, size_t n);

		/**
		 * @returns true if Headers are finished (State at BODY or OVER)
		 */
		bool	headersDone(void);

		/**
		 * @returns method, meaning the type of request
		 */
		string	getMethod(void) const;
		
		/**
		 * For Later
		 * PLACEHOLDER
		 */
		string	getUri(void) const;

		/**
		 * @returns path to file to access
		 */
		string	getPath(void) const;

		/**
		 * @returns Query, meaning additional information from the path
		 */
		string	getQuery(void) const;

		/**
		 * @returns Sesion ID
		 */
		string	getSessId(void) const;

		/**
		 * @returns Length of how many bytes should be in body
		 */
		size_t getBodyLength(void) const;

		/**
		 * @returns http version(s)
		 */
		string	getHttpVer(void) const;

		/**
		 * @returns buffer from what is left of parsing
		 */
		string getBuffer(void) const;

		std::string takeBuffer();

		/**
		 * @returns Headers from this response
		 */
		const Headers &getHeaders(void) const;

		/**
		 * @returns Cookies from this response
		 */
		CookieJar getCookies(void) const;

		/**
		 * @returns Body of the response as vector
		 */
		// vector<char> getBody(void) const;
		vector<char> getBody(void) const;
	
		// On-disk body helpers
		void	enableBodyOnDisk(const std::string &path);
		bool	isBodyOnDisk(void) const;
		std::string getBodyFilePath(void) const;
		// Read whole body into a vector (will read from disk if enabled)
		std::vector<char> readBodyToVector(void) const;
		// Remove body file (if any) and reset state
		void	cleanupBodyFile(void);

		/**
		 * @returns State of the Http Request
		 */
		enum HttpRequestState getState(void) const;

		/**
		 * @returns Amount of bytes this request has read so far
		 */
		size_t getTotalBytesRead(void) const;

		/**
		 * @returns Amount of bytes that were handled in total from this Request
		 * (meaning the amount of bytes you could free in total from your buffer)
		 */
		size_t getTotalBytesHandled(void) const;

		/**
		 * @returns Amount of bytes that was "handled" in the last parse() call
		 * @returns this represents the amount of bytes that can safely be removed from the calling buffer
		 */
		size_t getBytesHandledLast(void) const;

		void setKeepAlive(bool state);

		void reset();
};

/**
 * DANGER
 * PLACEHOLDER
 * USES THE ostream.write external function
 * which isnt allowed
 * DELETE LATER
 * ...or keep it for debugging
 */
std::ostream &operator<<(std::ostream &out, const HttpRequest &target);

#endif // HTTPREQUEST_H

