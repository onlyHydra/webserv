/* --- Listener.h --- */

/* ------------------------------------------
Author: undefined
Date: 8/12/2025
------------------------------------------ */

#ifndef LISTENER_H
#define LISTENER_H

#include "UniqueFD.h"
#include <string>
#include <vector>

/**
 * @class Listener
 * @brief Manages a network listening socket and its associated virtual servers.
 *
 * The Listener class encapsulates a single listening socket along with metadata
 * about which virtual servers should handle connections on that socket. It provides
 * RAII management of the file descriptor through UniqueFD and maintains the mapping
 * between network endpoints and virtual server configurations.
 *
 * ## Key Features:
 * - **RAII Socket Management**: Automatic cleanup via UniqueFD
 * - **Virtual Server Mapping**: Associates multiple virtual servers with one listener
 * - **IPv4/IPv6 Support**: Tracks protocol version for proper handling
 * - **Non-copyable**: Prevents accidental duplication of socket resources
 *
 * ## Usage Pattern:
 * 1. Create Listener with socket FD and network details
 * 2. Associate virtual server indices via setVirtualServerIndices()
 * 3. Register with event loop using getFD()
 * 4. Route incoming connections using virtualServerIndices()
 *
 * @note This class is non-copyable to prevent socket descriptor duplication.
 * @warning File descriptor ownership is transferred to UniqueFD on construction.
 */
class Listener
{
	private:
		UniqueFD fd;
		std::string host;
		int port;
		bool is_ipv6;
		std::vector<int> vs_indices;
		Listener(const Listener &);
		Listener &operator=(const Listener &);

	public:
		/**
		 * @brief Default constructor creates an invalid listener.
		 *
		 * Constructs a Listener with no associated file descriptor (invalid state).
		 * The listener will have port 0, empty host, IPv4 mode, and no virtual servers.
		 * This constructor is primarily used for container storage before proper initialization.
		 *
		 * @note The listener is in an invalid state until properly initialized.
		 * @note getFD() will return -1 for default-constructed listeners.
		 */
		Listener() : fd(), port(0), is_ipv6(false) {}

		/**
		 * @brief Constructs a Listener with a listening socket and network details.
		 *
		 * Takes ownership of the provided file descriptor and associates it with
		 * the specified network endpoint information. The file descriptor should
		 * already be bound, listening, and configured (non-blocking, close-on-exec).
		 *
		 * @param fd Valid listening socket file descriptor. Ownership transfers to UniqueFD.
		 * @param host Hostname or IP address the socket is bound to (e.g., "0.0.0.0", "localhost").
		 * @param port Port number the socket is listening on (1-65535).
		 * @param ipv6 True if the socket uses IPv6, false for IPv4.
		 * 
		 * @warning The file descriptor must be valid and properly configured before passing.
		 * @warning Caller must not use the fd parameter after construction (ownership transferred).
		 * @note No validation is performed on the parameters - they are stored as-is.
		 */
		Listener(int fd, std::string host, int port, bool ipv6) : fd(fd), host(host), port(port), is_ipv6(ipv6) {};

		/**
		 * @brief Returns the listening socket file descriptor.
		 *
		 * Provides access to the underlying file descriptor for event loop registration
		 * and socket operations. The file descriptor remains owned by the UniqueFD.
		 *
		 * @return File descriptor of the listening socket, or -1 if invalid.
		 * 
		 * @note The returned fd should only be used for read-only operations like event registration.
		 * @note Do not close or modify the returned file descriptor directly.
		 */
		int getFD() { return this->fd.get(); };

		/**
		 * @brief Returns the port number this listener is bound to.
		 *
		 * @return Port number (1-65535), or 0 for default-constructed listeners.
		 * 
		 * @note The port reflects the value passed during construction, not queried from the socket.
		 */
		int getPort() { return this->port; };

		/**
		 * @brief Returns the hostname or IP address this listener is bound to.
		 *
		 * @return String containing the host (e.g., "0.0.0.0", "localhost", "::1").
		 * 
		 * @note Returns a copy of the host string, safe to modify.
		 * @note The host reflects the value passed during construction, not queried from the socket.
		 */
		std::string getHost() { return this->host; };

		/**
		 * @brief Checks if this listener uses IPv6.
		 *
		 * @return True if the listening socket uses IPv6 protocol, false for IPv4.
		 * 
		 * @note Useful for protocol-specific handling and logging.
		 */
		bool IsIpv6() { return is_ipv6; };

		/**
		 * @brief Adds a virtual server index to this listener.
		 *
		 * Associates an additional virtual server with this listener. Virtual servers
		 * sharing the same (host, port) combination can use the same listener for
		 * efficiency. The index typically refers to a position in a ServerConfig array.
		 *
		 * @param idx Virtual server index. Should be a valid index into the server configuration.
		 * 
		 * @throws std::bad_alloc If vector reallocation fails during push_back.
		 * 
		 * @note No validation is performed on the index value.
		 * @note Consider using reserveVirtualServers() if you know the final count.
		 */
		void addVirtualServerIndex(int idx) { vs_indices.push_back(idx); }

		/**
		 * @brief Replaces all virtual server indices with the provided vector.
		 *
		 * Overwrites the current virtual server associations with a copy of the
		 * provided vector. This is the preferred method for bulk assignment.
		 *
		 * @param indices Vector of virtual server indices to copy.
		 * 
		 * @throws std::bad_alloc If vector allocation fails during copy.
		 * 
		 * @note The input vector is copied, not moved (C++98 compatibility).
		 * @note More efficient than multiple addVirtualServerIndex() calls.
		 */
		void setVirtualServerIndices(const std::vector<int> &indices)
		{
			vs_indices = indices;
		}

		/**
		 * @brief Replaces virtual server indices with values from a C-style array.
		 *
		 * Convenience method for setting virtual server indices from an array.
		 * The current indices are replaced with elements from arr[0] to arr[n-1].
		 *
		 * @param arr Pointer to array of integers. Must contain at least n valid elements.
		 * @param n Number of elements to copy from the array.
		 * 
		 * @throws std::bad_alloc If vector allocation fails during assign.
		 * 
		 * @warning arr must point to at least n valid integers.
		 * @warning Undefined behavior if arr is null and n > 0.
		 * @note More efficient than multiple addVirtualServerIndex() calls.
		 */
		void setVirtualServerIndices(const int *arr, size_t n)
		{
			vs_indices.assign(arr, arr + n);
		}

		/**
		 * @brief Pre-allocates memory for the specified number of virtual servers.
		 *
		 * Optimizes performance by reserving vector capacity before adding multiple
		 * virtual server indices. This prevents multiple reallocations during
		 * repeated addVirtualServerIndex() calls.
		 *
		 * @param n Expected number of virtual servers to be added.
		 * 
		 * @throws std::bad_alloc If memory reservation fails.
		 * 
		 * @note Does not change the vector size, only capacity.
		 * @note Call before multiple addVirtualServerIndex() operations for best performance.
		 */
		void reserveVirtualServers(size_t n) { vs_indices.reserve(n); }

		/**
		 * @brief Returns read-only access to the virtual server indices.
		 *
		 * Provides const access to the complete list of virtual server indices
		 * associated with this listener. Used for request routing and configuration
		 * queries.
		 *
		 * @return Const reference to the vector of virtual server indices.
		 * 
		 * @note The returned reference is valid until the next non-const operation on this listener.
		 * @note Prefer this over individual index access for iteration.
		 */
		const std::vector<int> &virtualServerIndices() const { return vs_indices; }

		/**
		 * @brief Returns the number of virtual servers associated with this listener.
		 *
		 * @return Number of virtual server indices currently stored.
		 * 
		 * @note Returns 0 for listeners with no associated virtual servers.
		 * @note Equivalent to virtualServerIndices().size() but more expressive.
		 */
		size_t virtualServerCount() const { return vs_indices.size(); }
		
		/**
		 * @brief Returns the virtual server index at the specified position.
		 *
		 * Provides bounds-checked access to virtual server indices by position.
		 * Useful for iterating through virtual servers when you need the index.
		 *
		 * @param i Position in the virtual server list (0-based).
		 * @return Virtual server index at position i.
		 * 
		 * @throws std::out_of_range If i >= virtualServerCount().
		 * 
		 * @warning Always check virtualServerCount() before calling to avoid exceptions.
		 * @note For iteration, prefer using virtualServerIndices() with iterators.
		 */
		int virtualServerIndexAt(size_t i) const { return vs_indices[i]; }

		/**
		 * @brief Efficiently swaps virtual server indices with another vector.
		 *
		 * Performs a constant-time swap of the internal virtual server indices
		 * with the provided vector. This is more efficient than assignment for
		 * large vectors and enables move-like semantics in C++98.
		 *
		 * @param other Vector to swap with. After the call, contains this listener's indices.
		 * 
		 * @note No memory allocation occurs - only internal pointers are swapped.
		 * @note Both vectors are modified: this gets other's contents, other gets this's contents.
		 * @note Useful for transferring indices without copying.
		 */
		void swapVirtualServerIndices(std::vector<int> &other) { vs_indices.swap(other); }
};

#endif // LISTENER_H
