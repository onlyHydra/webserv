/* --- Headers.cpp --- */

/* ------------------------------------------
author: Lilly81324
Date: 12/08/2025
------------------------------------------ */

#include "CiLess.h"

CiLess::CiLess(void)
{
}

CiLess::~CiLess(void)
{
}

bool CiLess::operator()(const string &left, const string &right) const
{
	size_t llen = left.size();
	size_t rlen = right.size();
	for (size_t i = 0; i < llen && i < rlen; ++i)
	{
		if (Util::tolower(left[i]) < Util::tolower(right[i]))
			return true;
		if (Util::tolower(left[i]) > Util::tolower(right[i]))
			return false;
	}
	return llen < rlen;
}
