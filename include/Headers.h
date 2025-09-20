#ifndef HEADERS_H
#define HEADERS_H

// Byte/entry limits (safe defaults if not provided elsewhere)
#ifndef HEADER_BYTE_LIMIT
# define HEADER_BYTE_LIMIT 4096
#endif
#ifndef HEADER_ENTRY_LIMIT
# define HEADER_ENTRY_LIMIT 1000
#endif

#include <cstddef>   // size_t
#include <map>
#include <ostream>
#include <string>

#include "CiLess.h"

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
		Headers();
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
		 * @param key: Key to remove
		 */
		void erase(const std::string &key);

		/**
		 * Deletes all entries in Header
		 */
		void clear();

		/**
		 * @returns Constant Iterator to beginning of Header
		 */
		std::map<std::string, std::string, CiLess>::const_iterator getBegin() const;

		/**
		 * @returns Constant Iterator to end of Header
		 */
		std::map<std::string, std::string, CiLess>::const_iterator getEnd() const;

		/**
		 * Returns whether key exists
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
		std::string serialize() const;

		/**
		 * Returns the value for the given key
		 * @param key: Key by which to identify the value from the map
		 * @returns Value at the given key or "" if non-existent
		 */
		std::string get(std::string key) const;

		/**
		 * @returns Amount of entries in Header
		 */
		int getLength() const;

		/**
		 * @returns Boolean whether map is empty or not
		 */
		bool isEmpty() const;

		/**
		 * @returns Amount of Bytes the Header currently has
		 */
		std::size_t getByteSize() const;

	private:
		std::map<std::string, std::string, CiLess> map;
		std::size_t byteSize;
};

	/**
	 * Displays Header on output
	 */
	std::ostream &operator<<(std::ostream &out, const Headers &target);

#endif // HEADERS_H
