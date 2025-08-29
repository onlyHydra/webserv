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

class ETagUtil {
public:
    // Strong-enough ETag for static files: size+mtime
    static std::string generate(const struct stat& st);
};

#endif // ETAGUTIL_H

