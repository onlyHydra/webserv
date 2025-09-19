/* --- CookieJar.h --- */

/* ------------------------------------------
author: Lilly81324
date: 11/08/2025
------------------------------------------ */

#ifndef COOKIEJAR_H
#define COOKIEJAR_H

#include <map>
#include <string>
#include "Headers.h"
#include "CiLess.h"
#include "HEADER_ENTRIES.h"

using namespace std;

// Used in set() to see if a source string still has data left in it to read
enum STRPOS
{
	SP_STUFF_LEFT,
	SP_END_REACH,
	SP_ERROR
};

/**
 * A Class representing a collection of Cookies in one Request / Response
 * -------------------------------------
 * Holds a map container for key - value pairs
 * Use the get() and set() functions to interact with the values
 * For Debugging, use output operator << to display all content
 * Only holds unique instances of keys, set() overwrites, does not add
 * -------------------------------------
 * This class will be instantiated by an HttpRequest, that it will be associated with
 * That Request will then populate this class by using its parse() function
 * HttpResponses should be instantiated with a HttpRequest during construction
 * so that we can automatically link the Requests CookieJar to the Responses CookieJar
 * This way, both share a single object, and parsing Cookies during Request Creation already
 * populates the Responses CookieJars fields
 * -------------------------------------
 * This class will end with the scope of a Request, but might in theory outlive a Response, linking to this
 * The CookieJar will end up being serialized and sent to the Client Socket, 
 * along with the Headers and the rest of the Response
 */
class CookieJar
{
	public:
		/**
		 * Constructor
		 */
		CookieJar();

		/**
		 * Destructor
		 */
		~CookieJar();

		/**
		 * UNUSED
		 * ?
		 * PLACEHOLDER / MISSING FUNCTION
		 * NEEDS TO BE IMPLEMENTED LATER
		 */
		void	setCookieHeaders(Headers &h, string name, string value, string path, int maxAge, bool httpOnly, bool secure, string sameSite);

		/**
		 * UNUSED
		 * Merges Header data into CookieJar data
		 * @param target: Target Header to use as source
		 */
		void	parseFrom(Headers &target);

		/**
		 * @brief Builds the CookieJars content from the given String
		 * @param head Header, which will monitor the HEADER_BYTE_LIMIT and HEADER_ENTRY_LIMIT
		 * @param source Source string, that holds key-value pairs as "key=value; " repeating
		 * @returns 400 HTTP_BAD_REQUEST if Syntax Error in string
		 * @returns 431 HTTP_HEADER_TOO_BIG if setting would exceed Header Limits
		 * @returns 200 HTTP_OK if nominal
		 */
		int	set(Headers &head, const std::string &source);

		/**
		 * @brief Removes key-value pair from CookieJar
		 * @param key: Key to remove
		 */
		void	erase(const string &key);

		/**
		 * @brief Deletes all entries in CookieJar
		 */
		void	clear(void);

		/**
		 * @brief Create Serialized string of Cookies
		 * @returns String of all the entries in this CookieJar in the follwing Syntax:
		 * "Set-Cookie: " <key> "=" <value> "\r\n"
		 * repeat for each key (of course without the '<' and '>' and with the string in quotes as literal)
		 */
		std::string serialize(void) const;


		/**
		 * Prepares the Cookies to be given into CGI enviroment as Enviroment variable
		 */
		std::string prepareForCgi(void) const;

		/**
		 * @returns Constant Iterator to beginning of CookieJar
		 */
		map<string, string, CiLess>::const_iterator	getBegin(void) const;

		/**
		 * @returns Constant Iterator to end of CookieJar
		 */
		map<string, string, CiLess>::const_iterator	getEnd(void) const;

		/**
		 * @brief Checks if given key is already registered in this CookieJar
		 * @param key Key to search
		 * @returns true if key exists (is registered)
		 */
		bool	keyExists(string key) const;
	
		/**
		 * UNUSED
		 * DEBUG FUNCTION
		 * @brief Displays the CookieJar on output stream
		 * @param out Output stream to display on
		 */
		void	show(ostream &out) const;

		/**
		 * @returns Amount of entries in CookieJar
		 */
		int		getLength(void) const;

		/**
		 * @returns Boolean wether cookies is empty or not
		 */
		bool	isEmpty(void) const;

		/**
		 * @brief Returns the value for the given key
		 * @param key: Key by which to identify the value from the cookies
		 * @returns Value at the given key or "" if non-existant
		 * @note For checking if key exists, use keyExists()
		 * @note Cannot be a const function, due to how cookies indexing works
		 */
		string	get(string key);
	private:
		map<string, string, CiLess> cookies;
};

/**
 * @brief Serializes (serialize()) the CookieJar for sending its contents
 */
ostream	&operator<<(ostream &out, const CookieJar &target);

#endif // COOKIEJAR_H
