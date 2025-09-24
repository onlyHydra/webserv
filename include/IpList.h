/* --- IpList.h --- */

/* ------------------------------------------
Author: lilly81324
Date: 22/09/2025
------------------------------------------ */

#ifndef IPLIST_H
# define IPLIST_H

# include <string>
# include <vector>
# include <unistd.h>
# include <sstream>
# include <stdlib.h>
# include <fcntl.h>
# include <sys/stat.h>
# include <netinet/in.h>
# include <sys/socket.h>
# include "Atoi.h"
# include "HttpResponse.h"
# include "ETagUtil.h"

/**
 * A specific Rule for allowed Ips
 * 127.0.0.1 -> 
 * base: 127.0.0.1 = 2130706433 
 * mask: 255.255.255.255 = 4294967295
 */
struct IpRule
{
	unsigned int base;
	unsigned int mask;
};

/**
 * Class for handling List of allowed and denied IPs
 * -------------------------------------------------
 * ServerConfigs parser calls on addAllowRule() and 
 * addDenyRule() to set the rules for IPs
 * -------------------------------------------------
 * AcceptorHandler gets the newly connecting IP with
 * getIpFromSocket() and then uses checkIp() to
 * check if this IP is allowed or not.
 * If not, then it calls upon ipDeniedResponse() and
 * sends that data directly to the Client,
 * then terminating the Connection
 * -------------------------------------------------
 * Theese use utility functions to covert the 
 * required and given data through using 
 * stringToIp() and getNetwork()
 */
class IpList
{
	public:
		// List of allowed IPs (highest precedence)
		std::vector<IpRule> allowed;
		// List of denied IPs (lower precedence)
		std::vector<IpRule> denied;
		// Wether IPs should by default be allowed (lowest precedence)
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
		 * @brief Gets the IP from a sockets adress storage
		 * @param source Address storage of the target socket
		 * @returns String consisting of the IP in common format (127.0.0.1)
		 */
		static std::string getIpFromSocket(struct sockaddr_storage *source);
		
		/**
		 * @brief Checks if a certain IP is allowed
		 * @param source Ip Adress + optional CIDR notation
		 * @note Checks if given IP matches one of the registerd IP ranges by checking against
		 * the Network Address (using Subnet Mask to ignore Network Bits)
		 * @note Order of scenarios, first to trigger will be end result: 
		 * Is allowed -> Is forbidden -> Default
		 */
		bool checkIp(const std::string &source) const;

		/**
		 * @brief Gives back serialized Response for rejecting because of denied IP
		 * @returns Serialized 403 Response
		 * 
		 * Tries to use 403 file and return that, and if it cant find or read,
		 * returns common fallback for 403
		 */
		static std::string ipDeniedResponse(void);

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
