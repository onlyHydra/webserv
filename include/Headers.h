/* --- Headers.h --- */

/* ------------------------------------------
Author: Lilly81324
Date: 11/08/2025
------------------------------------------ */

#ifndef HEADERS_H
# define HEADERS_H

# ifndef HEADER_BYTE_LIMIT
#  define HEADER_BYTE_LIMIT 4096
# endif
# ifndef HEADER_ENTRY_LIMIT
#  define HEADER_ENTRY_LIMIT 1000
# endif

# include <iostream>
# include <map>
# include "CiLess.h"

/**
 * A Class containing Header information
 * -------------------------------------
 * Holds a map container for key - value pairs
 * Use the get() and set() functions to interact with the values
 * For Debugging, use output operator << to display all content
 * Only holds unique instances of keys, set() overwrites, does not add
 */
class Headers
{
	public:
		/**
		 * Constructor
		 */
		Headers();
		/**
		 * Destructor
		 */
		~Headers();

		/**
		 * Sets value of given key or adds a new pair
		 * Operation stops if total with it would exceed HEADER_BYTE_LIMIT
		 * @param key: Key by which to identify the field
		 * @param value: New value to set the keys value to
		 * @returns true if operation suceeded
		 * @returns false if operation would exceeded HEADER_BYTE_LIMIT
		 * @returns false if operation would exceed HEADER_ENTRY_LIMIT
		 */
		bool set(std::string key, std::string value);
		/**
		 * Merge Header into this one
		 * @param src: Source Header that will be added on this one
		 */
		bool mergeFrom(const Headers &src);
		/**
		 * Removes key from Header
		 * Operation stops midway, if total with it would exceed HEADER_BYTE_LIMIT
		 * @param key: Key to remove
		 * @returns true if operation suceeded
		 * @returns false if operation exceeded HEADER_BYTE_LIMIT
		 */
		void erase(const std::string &key);
		/**
		 * Deletes all entries in Header
		 */
		void clear(void);

		/**
		 * @returns Constant Interator to beginning of Header
		 */
		std::map<std::string, std::string, CiLess>::const_iterator getBegin(void) const;
		/**
		 * @returns Constant Interator to end of Header
		 */
		std::map<std::string, std::string, CiLess>::const_iterator getEnd(void) const;
		/**
		 * Returns wether key exists
		 * @param key: Key to search
		 * @returns true if exists, false if not
		 */
		bool keyExists(std::string key) const;
		/**
		 * Displays the Header on output
		 * @param out: Output stream to show on
		 */
		void show(std::ostream &out) const;
		/**
		 * @returns string containing all key-values of Header
		 */
		std::string serialize(void) const;
		/**
		 * Returns the value for the given key
		 * @param key: Key by which to identify the value from the map
		 * @returns Value at the given key or "" if non-existant
		 * @note For checking if key exists, use get(std::string, int)
		 * @note Cannot be a const function, due to how map indexing works
		 */
		std::string get(std::string key);
		/**
		 * @returns Amount of entries in Header
		 */
		int	getLength(void) const;
		/**
		 * @returns Boolean wether map is empty or not
		 */
		bool isEmpty(void) const;
		/**
		 * @returns Amount of Bytes the Header currently has
		 */
		size_t	getByteSize(void) const;
	private:
		std::map<std::string, std::string, CiLess> map;
		size_t	byteSize;
};

/**
 * Displays Header on output
 */
std::ostream &operator<<(std::ostream &out, const Headers &target);

#endif // HEADERS_H
