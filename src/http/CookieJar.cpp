/* --- CookieJar.cpp --- */

/* ------------------------------------------
author: Lilly81324
date: 11/08/2025
------------------------------------------ */

#include "CookieJar.h"
#include "Headers.h"
#include "HTTPCODES.h"

using namespace std;

CookieJar::CookieJar(): cookies()
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
	(void)key;
	(void)value;
}

void CookieJar::parseFrom(Headers &target)
{
	map<string, string, CiLess>::iterator it;
	map<string, string, CiLess>::iterator end;

	end = target.getEnd();
	for (it = target.getBegin(); it != end; it++)
		this->set(target, std::string(it->first) + "=" + std::string(it->second));
}

/**
 * @brief Get a key-value pair from the source string
 * @param source Source string to remove substrings from
 * @param key String that gets set to Key from source string
 * @param value String that gets set to Value from source string
 * @returns 0 or SP_STUFF_LEFT if more pairs may be left in string (string not empty)
 * @returns 1 or SP_END_REACH if it found one last valid pair and then reached the end of string
 * @returns 2 or SP_ERROR if Syntax Error
 * 
 * Goes over the source string once, removing values from the start
 * So: 
 * Function called on ("abc=def", "", "")
 * Function ends on ("", "abc", "def")
 * Syntax:
 * Required: [key] [=] [value]
 * optional: [; ] [more key-value pairs]
 */
enum STRPOS makeCookiePairs(std::string &source, std::string &key, std::string &value)
{
	key = "";
	value = "";
	if (source.empty())
		return (SP_END_REACH);

	// Get Key
	size_t pos = 0;
	pos = source.find('=', 0);
	if (pos == std::string::npos)
		return (SP_ERROR);
	key = source.substr(0, pos);
	source = source.substr(pos + 1);

	// Get value
	pos = 0;
	while (source[pos] && source[pos] != ';')
		pos++;

	// Reached end of string -> whole string is value
	if (!source[pos])
	{
		value = source;
		source = "";
		return (SP_END_REACH);
	}

	// Reached semicolon -> everything before is value
	value = source.substr(0, pos);

	// Skip to next key
	pos++;
	while (source[pos] && source[pos] == ' ')
		pos++;
	source = source.substr(pos);
	return (SP_STUFF_LEFT);
}

int	CookieJar::set(Headers &head, const std::string &source)
{
	std::string copy = source;
	std::string key;
	std::string value;
	enum STRPOS state = SP_STUFF_LEFT;
	while (state == SP_STUFF_LEFT)
	{
		state = makeCookiePairs(copy, key, value);
		// If encountered error
		if (state == SP_ERROR)
			return (HTTP_BAD_REQUEST);
			
		// If No pair could be made (usually when end is reached)
		if (key.empty() && value.empty())
			break;

		// If Content, no error and valid pair, set that pair
		if (this->keyExists(key))
		{
			// If key already exists in CookieJar, try to replace its value
			if (!head.phantomReSet(this->get(key), value))
				return (HTTP_HEADER_TOO_BIG);
		}
		else
		{
			// If key is new in CookieJar, "add" it wholly
			if (!head.phantomSet(key, value))
				return (HTTP_HEADER_TOO_BIG);
		}
		this->cookies[key] = value;
	}
	return (HTTP_OK);
}

void CookieJar::erase(const string &key)
{
	this->cookies.erase(key);
}

void CookieJar::clear(void)
{
	this->cookies.clear();
}

std::string CookieJar::serialize(void) const
{
	std::map<std::string, std::string, CiLess>::const_iterator it;
	std::string out = "";

	for (it = this->cookies.begin(); it != this->cookies.end(); it++)
	{
		out.append(HDR_SET_COOKIE);
		out.append(": ");
		out.append(it->first);
		out.append("=");
		out.append(it->second);
		out.append("\r\n");
	}
	return (out);
}

std::string CookieJar::prepareForCgi(void) const
{
	std::string res;
	map<string, string, CiLess>::const_iterator start;
	map<string, string, CiLess>::const_iterator end;

	if (isEmpty())
		return ("");
	start = getBegin();
	end = getEnd();
	for (map<string, string, CiLess>::const_iterator it = start;
		it != end; it++)
	{
		// Add semicolon AFTER every pair, except the last one
		if (it != start)
			res += "; ";
		// Add the content
		res += it->first;
		res += "=";
		res += it->second;
	}
	return (res);
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
	return (this->cookies.size());
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
	out << target.serialize();
	return (out);
}
