/* --- Headers.cpp --- */

/* ------------------------------------------
author: Lilly81324
Date: 11/08/2025
------------------------------------------ */

#include "Headers.h"

Headers::Headers()
{
	this->byteSize = 0;
}

Headers::~Headers()
{
	this->byteSize = 0;
	this->map.clear();
}

bool Headers::set(std::string key, std::string value)
{
	if (this->getLength() + 1 > HEADER_ENTRY_LIMIT)
		return (false);
	if (this->keyExists(key))
	{
		if (this->byteSize + (value.size() - this->get(key).size()) > HEADER_BYTE_LIMIT)
			return (false);
		this->byteSize -= this->get(key).size();
	}
	else
	{
		if (this->byteSize + key.size() + value.size() > HEADER_BYTE_LIMIT)
			return (false);
		this->byteSize += key.size();
	}
	this->byteSize += value.size();
	this->map[key] = value;
	return (true);
}

bool	Headers::mergeFrom(const Headers &src)
{
	std::map<std::string, std::string, CiLess>::const_iterator it;
	bool okay = true;

	for(it = src.getBegin(); okay && it != src.getEnd(); it++)
	{
		okay = this->set(it->first, it->second);
	}
	return (okay);
}

void	Headers::erase(const std::string &key)
{
	this->byteSize -= (key.size() + get(key).size());
	this->map.erase(key);
}

void	Headers::clear(void)
{
	this->map.clear();
	this->byteSize = 0;
}

std::map<std::string, std::string, CiLess>::const_iterator Headers::getBegin(void) const
{
	return (this->map.begin());
}


std::map<std::string, std::string, CiLess>::const_iterator Headers::getEnd(void) const
{
	return (this->map.end());
}

bool Headers::keyExists(std::string key) const
{
	if (!this->map.count(key))
		return (false);
	return (true);
}

void	Headers::show(std::ostream &out) const
{
	std::map<std::string, std::string, CiLess>::const_iterator it;

	for(it = this->map.begin(); it != this->map.end(); it++)
	{
		if (it != this->map.begin())
			out << std::endl;
		out << "["  << it->first << "] : [" << it->second << "]";
	}
}

std::string Headers::serialize(void) const
{
	std::map<std::string, std::string, CiLess>::const_iterator it;
	std::string out = "";

	for(it = this->map.begin(); it != this->map.end(); it++)
	{
		out.append(it->first);
		out.append(": ");
		out.append(it->second);
		out.append("\r\n");
	}
	out += "\r\n";
	return (out);
}

std::string Headers::get(std::string key)
{
	if (!this->keyExists(key))
		return ("");
	return (this->map[key]);
}

int	Headers::getLength(void) const
{
	return (this->map.size());
}

bool	Headers::isEmpty(void) const
{
	return (this->map.empty());
}

size_t	Headers::getByteSize(void) const
{
	return (this->byteSize);
}

std::ostream &operator<<(std::ostream &out, const Headers &target)
{
	target.show(out);
	return (out);
}
