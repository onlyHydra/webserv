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

using namespace std;

enum STRPOS
{
	SP_STUFF_LEFT,
	SP_END_REACH,
	SP_ERROR
};

/**
 * A Class representing a Cookie
 * -------------------------------------
 * Holds a map container for key - value pairs
 * Use the get() and set() functions to interact with the values
 * For Debugging, use output operator << to display all content
 * Only holds unique instances of keys, set() overwrites, does not add
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
		void	parseFrom(const Headers &target);
		/**
		 * @brief Builds the CookieJars content from the given String
		 * @param source Source string, that holds key-value pairs as "key=value; " repeating
		 * @returns 400 HTTP_BAD_REQUEST if Syntax Error in string
		 * @returns 431 HTTP_HEADER_TOO_BIG if setting would exceed Header Limits
		 * @returns 200 HTTP_OK if nominal
		 */
		int	set(Headers &head, const std::string &value);
		/**
		 * Removes key from CookieJar
		 * @param key: Key to remove
		 */
		void	erase(const string &key);
		/**
		 * Deletes all entries in CookieJar
		 */
		void	clear(void);

		/**
		 * @returns Constant Iterator to beginning of CookieJar
		 */
		map<string, string>::const_iterator	getBegin(void) const;
		/**
		 * @returns Constant Iterator to end of CookieJar
		 */
		map<string, string>::const_iterator	getEnd(void) const;
		/**
		 * Returns the value for the given key and checks if it exists
		 * @param key: Key by which to identify the value from the cookies
		 * @param exists: Gets set to 1 if key exists, or 0 if not
		 * @returns Value at the given key or "" if non-existant
		 */
		bool	keyExists(string key) const;
		/**
		 * Displays the CookieJar on output
		 * @param out: Output stream to show on
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
		 * Returns the value for the given key
		 * @param key: Key by which to identify the value from the cookies
		 * @returns Value at the given key or "" if non-existant
		 * @note For checking if key exists, use get(string, int)
		 * @note Cannot be a const function, due to how cookies indexing works
		 */
		string	get(string key);
	private:
		map<string, string, CiLess> cookies;
};

/**
 * Displays CookieJar on output
 */
ostream	&operator<<(ostream &out, const CookieJar &target);

#endif // COOKIEJAR_H
