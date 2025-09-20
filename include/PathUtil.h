/* --- PathUtil.h --- */

/* ------------------------------------------
Author: undefined
Date: 8/10/2025
------------------------------------------ */

/* --- PathUtil.h --- */
#ifndef PATHUTIL_H
#define PATHUTIL_H

#include <string>

class PathUtil
	{
	public:
		// Absolute, normalized; follows symlinks if realpath() works.
		static bool canonicalize(const std::string &in, std::string &out);

		// Join a filesystem root with a URL path (usually starts with '/').
		static std::string joinRoot(const std::string &root, const std::string &urlPath);

		static bool isDir(const std::string &path);
		static bool isFile(const std::string &path);
};

#endif // PATHUTIL_H
