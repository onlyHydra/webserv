/* --- PathUtil.cpp --- */

/* ------------------------------------------
author: undefined
date: 8/10/2025
------------------------------------------ */

/* --- PathUtil.cpp --- */
#include "PathUtil.h"
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>
#include <vector>
#include <stdlib.h>  

static std::string normalize_slashes(const std::string &s)
{
    std::string out;
    out.reserve(s.size());
    bool last = false;
    for (size_t i = 0; i < s.size(); ++i)
    {
        char c = s[i];
        if (c == '/')
        {
            if (!last)
                out.push_back('/');
            last = true;
        }
        else
        {
            out.push_back(c);
            last = false;
        }
    }
    if (out.empty())
        out = "/";
    return out;
}

static std::string collapse_dotdot(const std::string &in)
{
    std::vector<std::string> parts;
    size_t i = 0;
    while (i < in.size())
    {
        size_t j = in.find('/', i);
        if (j == std::string::npos)
            j = in.size();
        std::string part = in.substr(i, j - i);
        if (part == "" || part == ".")
        { /* skip */
        }
        else if (part == "..")
        {
            if (!parts.empty())
                parts.pop_back();
        }
        else
        {
            parts.push_back(part);
        }
        i = j + 1;
    }
    std::string out = "/";
    for (size_t k = 0; k < parts.size(); ++k)
    {
        out += parts[k];
        if (k + 1 < parts.size())
            out += "/";
    }
    return out;
}

bool PathUtil::canonicalize(const std::string &in, std::string &out)
{
    if (in.empty())
    {
        out = "/";
        return true;
    }
    char buf[PATH_MAX];
#if defined(__linux__) || defined(__APPLE__)
    if (realpath(in.c_str(), buf) != 0)
    {
        out = buf;
        return true;
    }
#endif
    std::string s = in;
    if (s[0] != '/')
    {
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof(cwd)) == 0)
            return false;
        s = std::string(cwd) + "/" + s;
    }
    s = normalize_slashes(s);
    s = collapse_dotdot(s);
    out = s;
    return true;
}

std::string PathUtil::joinRoot(const std::string &root, const std::string &urlPath)
{
    std::string r = root.empty() ? "/" : root;
    if (!r.empty() && r[r.size() - 1] == '/')
        r.erase(r.size() - 1);
    std::string p = urlPath.empty() ? "/" : urlPath;
    if (p[0] != '/')
        p = "/" + p;
    return r + p;
}

static bool stat_type(const std::string &path, mode_t mask)
{
    struct stat st;
    if (::stat(path.c_str(), &st) != 0)
        return false;
    return (st.st_mode & S_IFMT) == mask;
}
bool PathUtil::isDir(const std::string &path) {
    return stat_type(path, S_IFDIR); 
}
bool PathUtil::isFile(const std::string &path) { 
    return stat_type(path, S_IFREG);
}
