// tests/unit/test_Listener.cpp
#include <catch2/catch_all.hpp>
#include <vector>
#include <algorithm>
#include <cerrno>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <unistd.h>

#include "Listener.h"

// Small helper to check if fd is closed
static bool is_fd_closed(int fd)
{
	errno = 0;
	return ::fcntl(fd, F_GETFD) == -1 && errno == EBADF;
}

TEST_CASE("Listener default-constructed is invalid")
{
	Listener L;
	REQUIRE(L.getFD() == -1); // assumes UniqueFD default is -1
	REQUIRE(L.getPort() == 0);
	REQUIRE(L.getHost().empty());
	REQUIRE_FALSE(L.IsIpv6());
	REQUIRE(L.virtualServerCount() == 0);
}

TEST_CASE("Listener takes ownership of FD and closes it (RAII)")
{
	// Use a socketpair for a cheap, valid FD 
	int fds[2];
	REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);

	int owned = fds[0];
	{
		Listener L(owned, "unix", 1234, false);
		REQUIRE(L.getFD() == owned);
		REQUIRE(L.getHost() == "unix");
		REQUIRE(L.getPort() == 1234);
		REQUIRE_FALSE(L.IsIpv6());
		// Don't touch 'owned' inside the block; L owns it now.
	}
	// After destruction, owned should be closed
	REQUIRE(is_fd_closed(owned));
	// Clean up peer end
	::close(fds[1]);
}

TEST_CASE("Listener manages virtual server indices (vector API)")
{
	// Create a dummy FD 
	int fd = ::socket(AF_INET, SOCK_STREAM, 0);
	REQUIRE(fd != -1);

	Listener L(fd, "0.0.0.0", 8080, false);

	std::vector<int> v{3, 4, 5};
	L.setVirtualServerIndices(v);
	REQUIRE(L.virtualServerCount() == 3);
	REQUIRE(L.virtualServerIndices()[0] == 3);
	REQUIRE(L.virtualServerIndices()[1] == 4);
	REQUIRE(L.virtualServerIndices()[2] == 5);

	L.addVirtualServerIndex(9);
	REQUIRE(L.virtualServerCount() == 4);
	REQUIRE(L.virtualServerIndices().back() == 9);

	std::vector<int> ext{7, 8};
	L.swapVirtualServerIndices(ext);
	REQUIRE(L.virtualServerCount() == 2);
	REQUIRE(L.virtualServerIndices()[0] == 7);
	REQUIRE(L.virtualServerIndices()[1] == 8);
	// ext now has the old {3,4,5,9}
	REQUIRE(ext.size() == 4);
	REQUIRE(ext.front() == 3);
	REQUIRE(ext.back() == 9);
}

TEST_CASE("Listener sets VS indices from C-array")
{
	int fd = ::socket(AF_INET, SOCK_STREAM, 0);
	REQUIRE(fd != -1);

	Listener L(fd, "127.0.0.1", 9000, false);

	int arr[] = {10, 20, 30, 40};
	L.setVirtualServerIndices(arr, 4);
	REQUIRE(L.virtualServerCount() == 4);
	REQUIRE(L.virtualServerIndices()[0] == 10);
	REQUIRE(L.virtualServerIndices()[3] == 40);
}

TEST_CASE("virtualServerIndexAt bounds behavior (choose ONE of these)")
{
	int fd = ::socket(AF_INET, SOCK_STREAM, 0);
	REQUIRE(fd != -1);
	Listener L(fd, "127.0.0.1", 9001, false);

	L.setVirtualServerIndices(std::vector<int>(1, 42)); // {42}

	// implement bounds-checking
	//   int virtualServerIndexAt(size_t i) const {
	//       if (i >= vs_indices.size()) throw std::out_of_range("virtualServerIndexAt");
	//       return vs_indices[i];
	//   }
	// then enable this:
	// REQUIRE_NOTHROW(L.virtualServerIndexAt(0));
	// REQUIRE_THROWS_AS(L.virtualServerIndexAt(1), std::out_of_range);

	// operator[] (no throws), then avoid calling out-of-bounds.
	REQUIRE(L.virtualServerIndexAt(0) == 42);
}

TEST_CASE("Optional: construct Listener from a real IPv4 listening socket")
{
	int fd = ::socket(AF_INET, SOCK_STREAM, 0);
	REQUIRE(fd != -1);

	int yes = 1;
	REQUIRE(::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == 0);

	sockaddr_in sa;
	std::memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // 127.0.0.1
	sa.sin_port = htons(0);						 // ephemeral
	REQUIRE(::bind(fd, (sockaddr *)&sa, sizeof(sa)) == 0);
	REQUIRE(::listen(fd, 8) == 0);

	socklen_t len = sizeof(sa);
	REQUIRE(::getsockname(fd, (sockaddr *)&sa, &len) == 0);
	int port = ntohs(sa.sin_port);
	REQUIRE(port > 0);

	{
		Listener L(fd, "127.0.0.1", port, false);
		REQUIRE(L.getFD() != -1);
		REQUIRE(L.getHost() == "127.0.0.1");
		REQUIRE(L.getPort() == port);
		REQUIRE_FALSE(L.IsIpv6());
	}
	// fd auto-closed by Listener
	REQUIRE(is_fd_closed(fd));
}
