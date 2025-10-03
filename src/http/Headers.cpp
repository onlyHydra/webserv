/* --- Headers.cpp --- */

/* ------------------------------------------
author: Lilly81324
Date: 11/08/2025
------------------------------------------ */

#include "Headers.h"

Headers::Headers() : map(), byteSize(0), realByteSize(0), entryCount(0), realEntryCount(0)
{
}

Headers::~Headers()
{
	this->map.clear();
}

bool Headers::phantomReSet(const std::string &oldValue, const std::string &newValue)
{
	// If Key already exists, account for the differenc of old and new value
	int diff = (int)newValue.size() - (int)oldValue.size();
	if (this->byteSize + diff > HEADER_BYTE_LIMIT)
		return (false);
	this->byteSize += diff;
	return (true);
}

bool Headers::phantomSet(const std::string &key, const std::string &value)
{
	// If key doesnt exist, account for keys length and values length
	if (entryCount + 1 > HEADER_ENTRY_LIMIT)
		return (false);
	if (this->byteSize + key.size() + value.size() > HEADER_BYTE_LIMIT)
		return (false);
	this->byteSize += key.size() + value.size();
	this->entryCount++;
	return (true);
}

bool Headers::set(std::string key, std::string value)
{
	if (entryCount + 1 > HEADER_ENTRY_LIMIT)
		return (false);
	std::size_t keySize = key.size();
	std::size_t oldSize = this->get(key).size();
	std::size_t newSize = value.size();
	int diff;
	if (this->keyExists(key))
	{
		// If Key already exists, account for the differenc of old and new value
		diff = (int)newSize - (int)oldSize;
		if (this->byteSize + diff > HEADER_BYTE_LIMIT)
			return (false);
		this->byteSize += diff;
		this->realByteSize += diff;
	}
	else
	{
		// If key doesnt exist, account for keys length and values length
		if (this->byteSize + keySize + newSize > HEADER_BYTE_LIMIT)
			return (false);
		this->byteSize += keySize + newSize;
		this->realByteSize += keySize + newSize;
		this->entryCount++;
		this->realEntryCount++;
	}
	this->map[key] = value;
	return (true);
}

void Headers::erase(const std::string &key)
{
	if (!keyExists(key))
		return ;
	this->byteSize -= (key.size() + get(key).size());
	this->map.erase(key);
}

void Headers::clear(void)
{
	this->map.clear();
	this->byteSize = 0;
	this->realByteSize = 0;
	this->entryCount = 0;
	this->realEntryCount = 0;
}

bool Headers::keyExists(std::string key) const
{
	if (!this->map.count(key))
		return (false);
	return (true);
}

std::string Headers::serialize(void) const
{
	std::map<std::string, std::string, CiLess>::const_iterator it;
	std::string out = "";

	for (it = this->map.begin(); it != this->map.end(); it++)
	{
		out.append(it->first);
		out.append(": ");
		out.append(it->second);
		out.append("\r\n");
	}
	out.append("\r\n");
	return (out);
}

std::string Headers::get(std::string key) const
{
	if (!this->keyExists(key))
		return ("");
	std::map<std::string, std::string, CiLess>::const_iterator it = this->map.find(key);
	return (it->second);
}
bool Headers::isEmpty(void) const
{
	return (this->map.empty());
}

std::ostream &operator<<(std::ostream &out, const Headers &target)
{
	// target.show(out);
	 out << target.serialize();
	return (out);
}
