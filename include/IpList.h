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
		 * @brief Adds the given IP adress to the list of allowed IPs
		 * @param source String containing IP and (optional) CIDR
		 * @returns false, if invaldi syntax in source string
		 * @returns true if valid IP
		 * 
		 * If exact entry already exists, does nothing, and returns true
		 */
		bool addAllowRule(const std::string &source);

		/**
		 * @brief Adds the given IP adress to the list of forbidden IPs
		 * @param source String containing IP and (optional) CIDR
		 * @returns false, if invaldi syntax in source string
		 * @returns true if valid IP
		 * 
		 * If exact entry already exists, does nothing, and returns true
		 */
		bool addDenyRule(const std::string &source);

		/**
		 * @brief Checks if a certain IP is allowed
		 * @param source Ip Adress + optional CIDR notation
		 * @note Order of scenarios, first to trigger will be end result: 
		 * Is allowed -> Is forbidden -> Default
		 */
		bool checkIp(const std::string &source) const;

		/**
		 * @brief Extracts IP adress and Subnet Mask from given Ip string
		 * @param source String containing IP adress, optionally with CIDR ("127.0.0.2/24")
		 * @param ip Set to the extracted IP adress as an unsigned integer
		 * @param masl Subnet Masks bit pattern as unsigned integer
		 * @returns false, if parsing error
		 * @returns true if all nominal
		 * @note ip and mask may be set, even when it returns false, however, you shouldnt use them then
		 * 
		 * If no CIDR notation is given, it will be assumed to be /32
		 */
		static bool stringToIp(const std::string &source, unsigned int &ip, unsigned int &mask);
		/**
		 * @brief Converts the given IP Adress and Subnet Mask into its Network Adress
		 * @param ip Ip adress as u int, prefferably from stringToIp
		 * @param mask Subnet Masks bit pattern as unsigned int, prefferably from stringToIp
		 * @returns Network Adress as uint
		 */
		static unsigned int getNetwork(unsigned int ip, unsigned int mask);
};

#endif // IPLIST_H
