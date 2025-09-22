/* --- IpList.h --- */

/* ------------------------------------------
Author: lilly81324
Date: 22/09/2025
------------------------------------------ */

#ifndef IPLIST_H
# define IPLIST_H

# include <string>
# include <vector>
# include "Atoi.h"

#include <iostream>

/**
 * A specific Rule for allowed Ips
 * 127.0.0.1 -> base: 127.0.0.1=2130706433 mask: 255.255.255.255=4294967295
 */
struct IpRule
{
	unsigned int base;
	unsigned int mask;
};

class IpList
{
	public:
		std::vector<IpRule> allowed;
		std::vector<IpRule> denied;
		bool defAllow;
		IpList(void);
		~IpList(void);
		/**
		 * Add a new rule for allowing Ip
		 */
		void addAllowRule(const std::string &source);
		/**
		 * Add a new rule for denying Ip
		 */
		void addDenyRule(const std::string &source);
		/**
		 * Checks, if a certain Ip is allowed
		 */
		bool checkIp(const std::string &source);
		static bool stringToIp(const std::string &source, unsigned int &ip, unsigned int &mask);
};

#endif // IPLIST_H

// Rule: allowIp 129.0.0.3/30
// Turin rule to uint: 2164260867
// Mask 30 to uint: 4294967292

// Bitwise AND ip and mask -> Only keep the bits for the network
// 2164260864
// This is the base
// We also store the 