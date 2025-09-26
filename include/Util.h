/* --- Util.h --- */

/* ------------------------------------------
Author: me
Date: 8/10/2025
------------------------------------------ */

#ifndef UTIL_H
#define UTIL_H

#include <unistd.h>

class Util {
	public:
		static char tolower(const char c);
		static char toupper(const char c);
		static bool isspace(const char c);
		static int	floor(double d);
		static int	ceil(double d);
		static void memset(void *s, int c, int n);
		static void memcpy(void *target, const void *source, int n);
		static void *memmove(void *dest, const void *src, int n);
		static void *memchr(const void *s, int c, int n);
	private:
		Util();
		~Util();
};

#endif // UTIL_H
