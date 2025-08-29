/* --- CookieJar.cpp --- */

/* ------------------------------------------
author: Lilly81324
date: 11/08/2025
------------------------------------------ */

#include "CookieJar.h"
#include "Headers.h"

using namespace std;

CookieJar::CookieJar()
{
}

CookieJar::~CookieJar()
{
	this->cookies.clear();
}

void CookieJar::setCookieHeaders(
	Headers &h, string key, string value, string path,
	int maxAge, bool httpOnly, bool secure, string sameSite)
{
	(void)h;
	(void)path;
	(void)maxAge;
	(void)httpOnly;
	(void)secure;
	(void)sameSite;
	this->set(key, value);
}

void CookieJar::parseFrom(const Headers &target)
{
	map<string, string, CiLess>::const_iterator it;
	map<string, string, CiLess>::const_iterator end;

	end = target.getEnd();
	for (it = target.getBegin(); it != end; it++)
		this->set(it->first, it->second);
}

void CookieJar::set(string key, string value)
{
	this->cookies[key] = value;
}

void CookieJar::erase(const string &key)
{
	this->cookies.erase(key);
}

void CookieJar::clear(void)
{
	this->cookies.clear();
}

map<string, string, CiLess>::const_iterator CookieJar::getBegin(void) const
{
	return (this->cookies.begin());
}

map<string, string, CiLess>::const_iterator CookieJar::getEnd(void) const
{
	return (this->cookies.end());
}

bool CookieJar::keyExists(string key) const
{
	if (!this->cookies.count(key))
		return (false);
	return (true);
}

void CookieJar::show(ostream &out) const
{
	map<string, string, CiLess>::const_iterator it;

	for (it = this->cookies.begin(); it != this->cookies.end(); it++)
	{
		if (it != this->cookies.begin())
			out << endl;
		out << "[" << it->first << "] : [" << it->second << "]";
	}
}

int CookieJar::getLength(void) const
{
	int count = 0;
	map<string, string, CiLess>::const_iterator it;

	for (it = this->getBegin(); it != this->getEnd(); it++)
		count++;
	return (count);
}

bool CookieJar::isEmpty(void) const
{
	return (this->cookies.empty());
}

string CookieJar::get(string key)
{
	if (!this->keyExists(key))
		return ("");
	return (this->cookies.find(key)->second);
}

ostream &operator<<(ostream &out, const CookieJar &target)
{
	target.show(out);
	return (out);
}
