/* --- Atoi.h --- */

/* ------------------------------------------
Author: undefined
Date: 8/10/2025
------------------------------------------ */

#ifndef ATOI_H
# define ATOI_H

# include <string>
# include <vector>

# ifndef MAX_INT
#  define MAX_INT 2147483647
# endif

# ifndef MIN_INT
#  define MIN_INT -2147483648
# endif


class Atoi
{
	public:
		static int atoi(const std::string &in);
		static unsigned int atoiIp(const std::string &in);
		static unsigned int atoiCidr(const std::string &in);
		static int	atoiHttpReq(const char *in, bool &error);
		static size_t atoiPatchOffset(const char *in);
	private:
		Atoi();
		~Atoi();
};

#endif // ATOI_H
