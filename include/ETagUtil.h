/* --- ETagUtil.h --- */

/* ------------------------------------------
Author: undefined
Date: 8/10/2025
------------------------------------------ */

/* --- ETagUtil.h --- */
#ifndef ETAGUTIL_H
#define ETAGUTIL_H

#include <string>
#include <sys/stat.h>
#include <sstream>

/**
 * Unique Identifiers for files
 * Further info in the generate() function
 * Source: https://www.rfc-editor.org/rfc/rfc9110.html#section-8.8.3
 */
class ETagUtil {
public:
    /**
	 * @note Generate Entity Tag for file
	 * @param filename Name for the file to generate off of
	 * @returns the created ETag
	 * 
	 * Generates unique identifier for a file, based off of its last state
	 * Meaning that if the file isnt modified, the generated ETag will always be the same result
	 * For non-existant/inaccesible files it generates an empty string
	 */
	static std::string generate(const char *filename);

	/**
	 * @brief Strong ETag Comparison
	 * @returns true if neither string is a weak Etag and both are equal
	 * @returns false otherwise
	 * @note Logic according to https://www.rfc-editor.org/rfc/rfc9110.html#section-8.8.3.2
	*/
	static bool strongComp(const std::string &s1, const std::string &s2);

	/**
	 * TODO: IF YOU WANT TO USE THIS, REWORK IT, THIS IS PROBABLY INSUFFICENT
	 * @brief Weak ETag Comparison
	 * @returns true if both strings are equal, ignoring weak/strong type
	 * @returns false otherwise
	*/
	static bool weakComp(const std::string &s1, const std::string &s2);
};

#endif // ETAGUTIL_H

