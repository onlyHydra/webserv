#include "IpList.h"

static bool	isDigit(char in)
{
	if (in >= '0' && in <= '9')
		return (true);
	return (false);
}

/**
 * Turns given CIDR number to Subnet Mask
 */
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
 * Read given file (used for 403.html)
 */
static bool readWholeFile(const std::string &path, std::vector<char> &out)
{
	int fd = ::open(path.c_str(), O_RDONLY);
	if (fd < 0)
		return false;
	char buf[8192];
	ssize_t n;
	while ((n = ::read(fd, buf, sizeof(buf))) > 0)
		out.insert(out.end(), buf, buf + n);
	int saved = errno;
	::close(fd);
	return (n >= 0) || (saved == 0);
}

/**
 * When 403.html error file cant be located, use this instead
 */
std::string fallback403(void)
{
	HttpResponse res;
	std::ostringstream out;

	res.setStatus(403);
	res.reason = "Forbidden";
	res.headers.set(HDR_CONTENT_LENGTH, "0");
	res.headers.set(HDR_CONTENT_TYPE, "text/plain");
	res.headers.set(HDR_CONNECTION, "close");
	out << res;
	return (out.str());
}

IpList::IpList(void): allowed(), denied(), defAllow(true)
{
}

IpList::~IpList(void)
{
	allowed.clear();
	denied.clear();
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

std::string IpList::getIpFromSocket(struct sockaddr_storage *source)
{
	// Turn storage into usable data struct (safe)
	struct sockaddr_in *in_adr = (struct sockaddr_in *)source;

	// Get stored Ip as unsigned int (4 octets), but in inverted order
	unsigned int un_int = (unsigned int)in_adr->sin_addr.s_addr;

	// Convert to char *, so we can iterate through the octets
	unsigned char *u_char = (unsigned char *)&un_int;

	// Iterate through, reversing the order of octects through stack operation on stream
	std::ostringstream out;
	out << (int)u_char[0] << "." << (int)u_char[1] << "." << (int)u_char[2] << "." << (int)u_char[3];
	return (out.str());
}

bool IpList::checkIp(const std::string &source) const
{
	unsigned int ip;
	unsigned int mask;
	size_t maxLoop = allowed.size();

	if (!IpList::stringToIp(source, ip, mask))
		return (false);

	// Check if its allowed
	for (size_t i = 0; i < maxLoop; i++)
	{
		if ((ip & allowed[i].mask) == allowed[i].base)
			return (true);
	}

	maxLoop = denied.size();
	// If not allowed, check if forbidden
	for (size_t i = 0; i < maxLoop; i++)
	{
		if ((ip & denied[i].mask) == denied[i].base)
			return (false);
	}

	// If neither, check what default is
	return (defAllow);
}

std::string IpList::ipDeniedResponse(void)
{
	HttpResponse res;
	std::ostringstream out;

	// Get error file path
	char *c_path = getcwd(NULL, 0);
	std::string path = "/";
	if (c_path)
	{
		path = std::string(c_path) + std::string("/www/errors/403.html");
		free(c_path);
	}

	// Check the error file
	struct stat st;
	if (::stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
		return (fallback403());
	
	// Read file into vector
	std::vector<char> file;
	if (!readWholeFile(path, file))
		return (fallback403());

	res.setStatus(HTTP_FORBIDDEN);
	res.headers.set(HDR_CONNECTION, "close");
	res.headers.set(HDR_ETAG, ETagUtil::generate(path.c_str()));
	res.body.clear();
	res.body.assign(file.begin(), file.end());
	res.headers.set(HDR_CONTENT_TYPE, "text/html");
	std::ostringstream cl;
	cl << static_cast<unsigned long>(file.size());
	res.headers.set(HDR_CONTENT_LENGTH, cl.str());
	res.bodyLength = file.size();
	out << res;
	return (out.str());
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
