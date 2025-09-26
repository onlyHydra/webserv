/* --- Util.cpp --- */

/* ------------------------------------------
author: undefined
date: 8/10/2025
------------------------------------------ */

#include "Util.h"

char Util::tolower(const char c)
{
	if (c >= 'A' && c <= 'Z')
		return (c + ('a' - 'A'));
	return (c);
}

char Util::toupper(const char c)
{
	if (c >= 'A' && c <= 'Z')
		return (c - ('a' - 'A'));
	return (c);
}
