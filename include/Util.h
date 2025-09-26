/* --- Util.h --- */

/* ------------------------------------------
Author: me
Date: 8/10/2025
------------------------------------------ */

#ifndef UTIL_H
#define UTIL_H

class Util {
	public:
		static char tolower(const char c);
		static char toupper(const char c);
		static bool isspace(const char c);
	private:
		Util();
		~Util();
};

#endif // UTIL_H
