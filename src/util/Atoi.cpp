/* --- Atoi.cpp --- */

/* ------------------------------------------
author: undefined
date: 8/10/2025
------------------------------------------ */

#include "Atoi.h"

Atoi::Atoi()
{
	// Constructor
}

Atoi::~Atoi()
{
	// Destructor
}

static bool	isDigit(char in)
{
	if (in >= '0' && in <= '9')
		return (true);
	return (false);
}

/**
 * @brief Simple, basic string to int conversion
 * @param in String to convert
 * @returns Integer based on the input (0 if error)
 * Skips whitespace, handles one sign, no overflow checks
 */
int	Atoi::atoi(const std::string &in)
{
	int	i;
	int	fac;
	int	result;

	i = 0;
	fac = 1;
	while (in[i] == ' ' || in[i] == '\n' || in[i] == '\t' \
|| in[i] == '\v' || in[i] == '\f' || in[i] == '\r')
		i++;
	if (in[i] == '-' || in[i] == '+')
	{
		if (in[i] == '-')
			fac = fac * -1;
		i++;
	}
	result = 0;
	while (isDigit(in[i]))
	{
		result = (result * 10) + (in[i] - '0');
		i++;
	}
	return (result * fac);
}

/**
 * @brief Handles at most 3 digit long number
 */
unsigned int Atoi::atoiIp(const std::string &in)
{
	unsigned int result = 0;
	for (std::size_t i = 0; i < 3 && i < in.length(); i++)
		result = (result * 10) + (in[i] - '0');
	return (result);
}

/**
 * @brief Handles at most 2 digit long number
 */
unsigned int Atoi::atoiCidr(const std::string &in)
{
	unsigned int result = 0;
	size_t start = 0;

	// If no specified mask -> 32
	if (in.empty())
		return (32);

	// Skip leading /
	if (in[0] == '/')
		start++;

	// Take at most 2 digits
	for (size_t i = start; i < start + 2 && i < in.length(); i++)
		result = (result * 10) + (in[i] - '0');
	return (result);
}

/**
 * @brief Parser for Http Request Content Length Parameter
 * @param in String to convert
 * @param error will be set to true, if operation fails
 * @returns Integer based on the input (0 if error)
 * @note Only specific whitespaces, then at most 1 plus, then some number
 */
int	Atoi::atoiHttpReq(const char *in, bool &error)
{
	int	i;
	long	result;

	error = true;
	i = 0;
	while (in[i] == ' ' || in[i] == '\t' || in[i] == '\v' || in[i] == '\f')
		i++;
	if (in[i] == '+')
		i++;
	if (!isDigit(in[i]))
		return (0);
	result = 0;
	while (in[i] >= '0' && in[i] <= '9')
	{
		result = (result * 10) + (in[i] - '0');
		i++;
		if (result > (long)MAX_INT)
		{
			error = false;
			return (MAX_INT);
		}
	}
	if (in[i])
		return (0);
	error = false;
	return (result);
}

/**
 * @brief Parser for Patch method which requires a file offset
 * @param in String to convert
 * @returns size_t based on the input (0 if error)
 * @note Only specific whitespaces, then at most 1 plus, then some number
 */
size_t	Atoi::atoiPatchOffset(const char *in)
{
	int	i;
	size_t	result;

	i = 0;
	while (in[i] == ' ' || in[i] == '\t' || in[i] == '\v' || in[i] == '\f')
		i++;
	if (in[i] == '+')
		i++;
	if (!isDigit(in[i]))
		return (0);
	result = 0;
	while (isDigit(in[i]))
	{
		result = (result * 10) + (in[i] - '0');
		i++;
	}
	if (in[i])
		return (0);
	return (result);
}