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
		 * @brief Simulates setting a new entry, without actually doing so
		 * @param key Key by which to identify the entry
		 * @param value Value for that entry
		 * @returns true if operation suceeded
		 * @returns false if operation would exceeded HEADER_BYTE_LIMIT
		 * @returns false if operation would exceed HEADER_ENTRY_LIMIT
		 * @warning ONLY DO THIS, IF THE KEY IS NEW! (use keyExists())
		 * 
		 * This is for checking and registering if the new key could be added
		 * without actually adding it. 
		 * However, we do (try to) change the byte and entry count.
		 * This is used in the CookieJar, because it should in theory also be in the Header
		 * so the tracking of the amount of resouces is done in the Header, while
		 * we store the actual Cookies inside the CookieJar
		 */
		bool phantomSet(const std::string &key, const std::string &value);

		/**
		 * @brief Simulates replacing an existing entry, without actually doing so
		 * @param oldValue Old value, currently stored
		 * @param value New value to replace the old value
		 * @returns true if operation suceeded
		 * @returns false if operation would exceeded HEADER_BYTE_LIMIT
		 * @returns false if operation would exceed HEADER_ENTRY_LIMIT
		 * @warning ONLY DO THIS, IF THE KEY ALREADY EXISTS! (use keyExists())
		 * 
		 * This is for checking and registering if the current value can be replaced
		 * with the new Value, without actually replacing it
		 * However, we do (try to) change the byte and entry count.
		 * This is used in the CookieJar, because it should in theory also be in the Header
		 * so the tracking of the amount of resouces is done in the Header, while
		 * we store the actual Cookies inside the CookieJar
		 */
		bool phantomReSet(const std::string &oldValue, const std::string &newValue);

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
		bool mergeFrom(Headers &src);

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
		 * @returns Iterator to beginning of Header
		 */
		std::map<std::string, std::string, CiLess>::iterator getBegin();

		/**
		 * @returns Iterator to end of Header
		 */
		std::map<std::string, std::string, CiLess>::iterator getEnd();

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
		 * @returns Amount of Bytes the Header and Cookies currently have
		 */

		std::size_t getByteSize() const;
		/**
		 * @returns Amount of Bytes the Header currently has
		 */

		size_t getRealByteSize(void) const;

		/**
		 * @returns Amount of Entries in Header and Cookies
		 */
		size_t getEntryCount(void) const;

		/**
		 * @returns Amount of Entries in Header
		 */
		size_t getRealEntryCount(void) const;

	private:
		std::map<std::string, std::string, CiLess> map;
		std::size_t byteSize;
		std::size_t realByteSize;
		std::size_t entryCount;
		std::size_t realEntryCount;
};

	/**
	 * Displays Header on output
	 */
	std::ostream &operator<<(std::ostream &out, const Headers &target);

#endif // HEADERS_H
