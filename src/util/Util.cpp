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

void Util::memset(void *s, int c, int n)
{
	char *s_c = (char *)s;
	int i = 0;
	while (i < n)
	{
		s_c[i] = c;
		i++;
	}
}

void Util::memcpy(void *target, const void *source, int n)
{
	char *target_c = (char *)target;
	char *source_c = (char *)source;
	int i = 0;
	while (i < n)
	{
		target_c[i] = source_c[i];
		i++;
	}
}

void *Util::memmove(void *dest, const void *src, int n)
{
	if (n == 0 || dest == src)
		return dest;

	unsigned char *dest_c = (unsigned char *)dest;
	const unsigned char *src_c = (const unsigned char *)src;

	if (dest_c < src_c)
	{
		while (n--)
			*dest_c++ = *src_c++;
	}
	else
	{
		dest_c += n;
		dest_c += n;
		while (n--)
			*--dest_c = *--src_c;
	}
	return (dest);
}

void *Util::memchr(const void *s, int c, int n)
{
	const unsigned char *s_c = (const unsigned char *)s;

	while (n--)
	{
		if (*s_c == (unsigned char)c)
			return (void *)s_c;
		++s_c;
	}
	return (NULL);
}
