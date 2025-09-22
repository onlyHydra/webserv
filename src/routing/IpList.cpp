#include "IpList.h"

static bool	isDigit(char in)
{
	if (in >= '0' && in <= '9')
		return (true);
	return (false);
}

unsigned int cidrToMask(unsigned int cidr)
{
	unsigned int current = 0;
	unsigned int loop = 32 - cidr;

	for (unsigned int i = 0; i < loop; i++)
	{
		current = current << 1;
		current = current + 1;
	}
	current = ~ current;
	return (current);
}

IpList::IpList(void): allowed(), denied(), defAllow(true)
{
}

IpList::~IpList(void)
{
	allowed.clear();
	denied.clear();
}

bool IpList::checkIp(const std::string &source) const
{
	unsigned int ip;
	unsigned int mask;
	unsigned int netAdr;
	size_t maxLoop = allowed.size();

	if (!IpList::stringToIp(source, ip, mask))
		return (false);
	netAdr = IpList::getNetwork(ip, mask);

	// Check if its allowed
	for (size_t i = 0; i < maxLoop; i++)
	{
		if (allowed[i].base == netAdr && allowed[i].mask == mask)
			return (true);
	}

	// If not allowed, check if forbidden
	for (size_t i = 0; i < maxLoop; i++)
	{
		if (denied[i].base == netAdr && denied[i].mask == mask)
			return (false);
	}

	// If neither, check what default is
	return (defAllow);
}

bool IpList::addAllowRule(const std::string &source)
{
	unsigned int ip;
	unsigned int mask;
	unsigned int networkAdr;
	size_t maxLoop = allowed.size();

	if (!IpList::stringToIp(source, ip, mask))
		return (false);
	networkAdr = getNetwork(ip, mask);

	for (size_t i = 0; i < maxLoop; i++)
	{
		if (allowed[i].base == networkAdr && allowed[i].mask == mask)
			return (true);
	}
	IpRule rule;
	rule.base = networkAdr;
	rule.mask = mask;
	allowed.push_back(rule);
	return (true);
}

bool IpList::addDenyRule(const std::string &source)
{
	unsigned int ip;
	unsigned int mask;
	unsigned int networkAdr;
	size_t maxLoop = denied.size();

	if (!IpList::stringToIp(source, ip, mask))
		return (false);
	networkAdr = getNetwork(ip, mask);

	for (size_t i = 0; i < maxLoop; i++)
	{
		if (denied[i].base == networkAdr && denied[i].mask == mask)
			return (true);
	}
	IpRule rule;
	rule.base = networkAdr;
	rule.mask = mask;
	denied.push_back(rule);
	return (true);
}

bool IpList::stringToIp(const std::string &source, unsigned int &ip, unsigned int &mask)
{
	unsigned int octet;
	unsigned int cidr;
	bool cidrGiven = false;
	int i = 0;
	int start = 0;
	ip = 0;
	// Pre-check
	if (source.empty())
		return (false);

	// For each octet:
	for (int iter = 0; iter < 4; iter++)
	{
		// Skip mandatory dot, unless at last octet
		if (iter > 0)
		{
			if (source[i] == '.')
				i++;
			else
				return (false);
		}
		start = i;

		// Numbers mandatory
		if (!isDigit(source[i]))
			return (false);

		// Get count of nums, at most 3
		while (isDigit(source[i]) && i < start + 3)
			i++;

		// Cut out an octet between dots or ends
		octet = Atoi::atoiIp(source.substr(start, i - start));
		if (octet > 255)
			return (false);
		
		// Bitshift to the left, as if octet was an unsigned char
		ip += octet << ((3 - iter) * 8);
	}

	// Skip optional
	if (source[i] == '/')
	{
		cidrGiven = true;
		i++;
	}
	start = i;

	// If no CIDR expected, should end here
	if (!cidrGiven && source[i] != '\0')
		return (false);

	// CIDR expected -> number mandatory
	if (cidrGiven && !isDigit(source[i]))
		return (false);
	
	// Get CIDR Notation
	if (!cidrGiven)
	{
		cidr = 32;
	}
	else
	{
		while (isDigit(source[i]) && i < start + 2)
			i++;
		cidr = Atoi::atoiCidr(source.substr(start, i - start));
	}

	// Invalid CIDR
	if (cidr > 32)
		return (false);

	// No trailing content
	if (source[i] != '\0')
		return (false);
	mask = cidrToMask(cidr);
	return (true);
}

unsigned int IpList::getNetwork(unsigned int ip, unsigned int mask)
{
	return (ip & mask);
}
