/* --- ETagUtil.cpp --- */

/* ------------------------------------------
author: undefined
date: 8/10/2025
------------------------------------------ */

/* --- ETagUtil.cpp --- */
#include "ETagUtil.h"
#include <sstream>
#include <iomanip>

std::string ETagUtil::generate(const struct stat &st)
{
    unsigned long long size = static_cast<unsigned long long>(st.st_size);
    unsigned long long msec = static_cast<unsigned long long>(st.st_mtime);
#if defined(__APPLE__) || defined(st_mtim)
#ifdef __APPLE__
    msec = static_cast<unsigned long long>(st.st_mtimespec.tv_sec) * 1000ULL + static_cast<unsigned long long>(st.st_mtimespec.tv_nsec / 1000000ULL);
#else
    msec = static_cast<unsigned long long>(st.st_mtim.tv_sec) * 1000ULL + static_cast<unsigned long long>(st.st_mtim.tv_nsec / 1000000ULL);
#endif
#endif
    std::ostringstream oss;
    oss << "\"wsv-" << std::hex << size << "-" << msec << "\"";
    return oss.str();
}
