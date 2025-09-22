#include "IpList.h"

IpList::IpList(void): allowed(), denied(), defAllow(true)
{
}

IpList::~IpList(void)
{
	allowed.clear();
	denied.clear();
}

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

/**
 * Add a new rule for allowing Ip
 * Input should be something like xxx.xxx.xxx.xxx/xx
 * where xxx is numbers from 0-255, xx is from 0-32
 * and "/xx" is optional, but if missing, will be assumed to be "/32"
 */
// void addAllowRule(const std::string &source)
// {
// 	bool singleIp = false;
// 	if (source.empty())
// 		return ;
// 	if (source.find("/") == std::string::npos)
// 		singleIp = true;
// }

// XXX .YYY .ZZZ .AAA /24

/**
 * Returns false, when 
 */
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
