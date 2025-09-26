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

bool Util::isspace(const char c)
{
	if (c == ' ' || c == '\f' || c == '\n' || c == '\r' || c == '\t' || c == '\v')
		return (true);
	return (false);
}

int Util::floor(double d)
{
	int i = static_cast<int>(d);
	if (d < 0 && d != static_cast<double>(i))
		return i - 1;
	return i;
}

int Util::ceil(double d)
{
	int i = static_cast<int>(d);
	if (d > 0 && d != static_cast<double>(i))
		return i + 1;
	return i;
}

