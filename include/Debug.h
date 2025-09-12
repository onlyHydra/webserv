#ifndef DEBUG_CGI
#define DEBUG_CGI 0
#endif

#pragma once
#include <cstdio>
#if DEBUG_CGI
#define DBGf(...)  do { std::fprintf(stderr, __VA_ARGS__); std::fflush(stderr); } while(0)
#else
#  define DBGf(...)  do {} while(0)
#endif
