/* --- UniqueFD.h --- */

/* ------------------------------------------
Author: undefined
Date: 8/11/2025
------------------------------------------ */

#ifndef UNIQUEFD_H
#define UNIQUEFD_H

#include <unistd.h>

class UniqueFD
{

	public:
		explicit UniqueFD(int fd = -1)
			: fd(fd) {}

		~UniqueFD()
		{
			if (fd != -1)
			{
				::close(fd);
			}
		}

	private:
		int fd;
		UniqueFD(const UniqueFD &);
		UniqueFD &operator=(const UniqueFD &);

	public:
		int get() const { return fd; }

		operator bool() const { return fd != -1; }

		int release()
		{
			int temp = fd;
			fd = -1;
			return temp;
		}

		void reset(int newFd = -1)
		{
			if (fd != -1)
			{
				::close(fd);
			}
			fd = newFd;
		}
};

#endif // UNIQUEFD_H
