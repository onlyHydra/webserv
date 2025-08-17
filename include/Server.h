/* --- Server.h --- */

/* ------------------------------------------
Author: undefined
Date: 8/10/2025
------------------------------------------ */

#ifndef SERVER_H
#define SERVER_H

#ifdef USE_STUBS
  #include "stubs/EventLoop.h"
  #include "stubs/ServerConfig.h"
#else
  #include "EventLoop.h"
  #include "ServerConfig.h"
#endif

#include "Listener.h"
#include "ClientConnection.h"
#include <vector>
#include <string>
#include <poll.h>
#include <map>





/**
 * @class Server
 * @brief High-level web server manager implementing virtual hosting and event-driven I/O.
 *
 * The Server class orchestrates the complete lifecycle of a multi-virtual-host web server.
 * It handles listener creation, event loop management, and provides efficient request routing
 * based on Host headers. The implementation follows RAII principles and is designed for
 * C++98 compatibility.
 *
 * ## Key Features:
 * - **Virtual Hosting**: Multiple virtual servers can share listeners on the same port
 * - **Event-Driven**: Non-blocking I/O with epoll/kqueue event loop integration  
 * - **Robust Networking**: IPv4/IPv6 dual-stack support with comprehensive error handling
 * - **Memory Safe**: RAII-based resource management prevents leaks under all conditions
 * - **Fast Routing**: O(log n) Host header lookup via pre-built hostname maps
 *
 * ## Lifecycle:
 * 1. **Construction**: Server(config) - Validates configuration, initializes empty state
 * 2. **Startup**: start() - Creates listeners, registers with event loop, builds routing maps
 * 3. **Operation**: Event loop processes connections, routes requests via host maps
 * 4. **Shutdown**: stop() or destructor - Graceful cleanup of all resources
 *
 * ## Thread Safety:
 * This class is **not thread-safe**. All operations must be performed from a single thread.
 *
 * ## Exception Safety:
 * - **Constructor**: Basic guarantee - object construction may fail but no leaks
 * - **start()**: Strong guarantee - either fully succeeds or leaves server in stopped state  
 * - **stop()**: No-throw guarantee - always succeeds and cleans up resources
 * - **Destructor**: No-throw guarantee - automatic cleanup via RAII
 *
 * @warning ServerConfig reference must remain valid for the Server's entire lifetime.
 * @note Designed for single-threaded event-driven servers. Use multiple Server instances for multi-threading.
 */
class Server
{
	private:
		EventLoop loop;
		ServerConfig &srvConfig;
		std::vector<Listener*> listeners;
		std::map<int, std::map<std::string, int> > host_map_by_port; // port -> (host -> vs_index)
		std::map<int, int> default_vs_by_port;						 // port -> default vs_index

		/**
		 * @brief Creates a raw listening socket for the specified host and port.
		 *
		 * This method performs comprehensive socket creation using getaddrinfo() to resolve
		 * the host/port combination and tries each returned address until one successfully
		 * binds and listens. The socket is configured as non-blocking with SO_REUSEADDR
		 * and FD_CLOEXEC flags.
		 *
		 * @param host The hostname or IP address to bind to. Use "0.0.0.0" for all interfaces.
		 * @param port The port number to listen on (1-65535).
		 * @param out_is_ipv6 Output parameter set to true if the created socket is IPv6, false for IPv4.
		 * 
		 * @return File descriptor of the successfully created and bound listening socket.
		 * 
		 * @throws std::runtime_error If getaddrinfo() fails with DNS resolution details.
		 * @throws std::runtime_error If no address candidates succeed in bind/listen with errno details.
		 * @throws std::runtime_error If fcntl() operations fail during socket configuration.
		 * 
		 * @warning The returned file descriptor must be managed properly to avoid leaks.
		 * @note Uses RAII internally with UniqueFD to prevent leaks during address iteration.
		 * @note Prefers the first working address from getaddrinfo() results.
		 */
		int createListenSocketRaw(const std::string &host, int port, bool &out_is_ipv6);
		
		/**
		 * @brief Registers all active listeners with the event loop.
		 *
		 * Iterates through the listeners vector and adds each valid listener's
		 * file descriptor to the event loop for read event monitoring (EV_READ).
		 * Skips null pointers and invalid file descriptors.
		 *
		 * @throws std::exception May propagate exceptions from EventLoop::addFD().
		 * 
		 * @warning Should only be called after listeners have been successfully created.
		 * @note Assumes EV_READ constant equals 1 for event loop registration.
		 */
		void registerListeners();
		
		/**
		 * @brief Unregisters all listeners from the event loop.
		 *
		 * Removes all listener file descriptors from event loop monitoring.
		 * Safe to call multiple times. Skips null pointers and invalid file descriptors.
		 *
		 * @throws std::exception May propagate exceptions from EventLoop::removeFD().
		 * 
		 * @note Does not close the file descriptors, only removes them from monitoring.
		 * @note Called during server shutdown to clean up event loop state.
		 */
		void unregisterListeners();
		
		/**
		 * @brief Closes all listeners and clears the listeners vector.
		 *
		 * Destroys all Listener objects in the listeners vector, which automatically
		 * closes their associated file descriptors through RAII. The vector is then
		 * cleared to remove all references.
		 *
		 * @note Does not throw exceptions. RAII destructors handle cleanup automatically.
		 * @note Safe to call multiple times. No-op if listeners vector is already empty.
		 */
		void closeAll();
		
		/**
		 * @brief Builds a plan for listener creation based on unique host/port combinations.
		 *
		 * Analyzes the server configuration to identify unique (host, port) pairs
		 * and groups virtual servers by these pairs. This allows multiple virtual
		 * servstders to share the same listener when they use the same host:port combination.
		 *
		 * @param unique_pairs Output vector populated with unique (host, port) pairs found in configuration.
		 * @param vs_indices_by_pair Output map where keys are (host, port) pairs and values are vectors 
		 *                           of virtual server indices that should use that listener.
		 * 
		 * @throws std::bad_alloc If memory allocation fails during container operations.
		 * 
		 * @warning Output parameters are modified. Ensure they are empty or prepared for overwriting.
		 * @note Uses "0.0.0.0" as default host for empty listen_host configurations.
		 * @note Essential for implementing virtual hosting with minimal socket resources.
		 */
		void buildListenerPlan(std::vector<std::pair<std::string, int> > &unique_pairs,
							std::map<std::pair<std::string, int>, std::vector<int> > &vs_indices_by_pair);
		
		/**
		 * @brief Constructs hostname-to-virtual-server mappings for fast request routing.
		 *
		 * Builds two critical data structures for HTTP Host header routing:
		 * 1. host_map_by_port: Maps port -> (lowercase_hostname -> virtual_server_index)
		 * 2. default_vs_by_port: Maps port -> default_virtual_server_index
		 *
		 * Server names are normalized to lowercase for case-insensitive matching.
		 * The first virtual server for each port becomes the default fallback.
		 *
		 * @throws std::bad_alloc If memory allocation fails during map construction.
		 * 
		 * @warning Clears existing host maps before rebuilding. Call after listeners are finalized.
		 * @note Must be called after listeners are created and virtual server indices are assigned.
		 * @note Enables O(log n) Host header lookup during request processing.
		 */
		void buildHostMaps();

	public:
		/**
		 * @brief Constructs a Server instance with the given configuration.
		 *
		 * Initializes the server with a reference to the server configuration and
		 * creates an empty EventLoop. No network operations are performed during
		 * construction - call start() to begin listening for connections.
		 *
		 * @param srvConfig Reference to ServerConfig containing virtual server definitions,
		 *                  listen addresses, ports, and other server settings. Must remain
		 *                  valid for the lifetime of this Server instance.
		 * 
		 * @throws std::bad_alloc If memory allocation fails during initialization.
		 * 
		 * @warning The srvConfig reference must remain valid throughout the Server's lifetime.
		 * @note Constructor performs minimal work. Heavy initialization occurs in start().
		 * @note The server is in a stopped state after construction.
		 */
		explicit Server(ServerConfig &srvConfig);
		
		/**
		 * @brief Destructor. Automatically stops the server and cleans up all resources.
		 *
		 * Ensures proper cleanup by calling stop() which unregisters listeners from
		 * the event loop and closes all listening sockets. RAII ensures no resource
		 * leaks even if exceptions occur during destruction.
		 *
		 * @note Does not throw exceptions. All cleanup is performed via RAII.
		 * @note Safe to destroy a Server that was never started or already stopped.
		 */
		~Server();
		
		/**
		 * @brief Starts the server and begins accepting connections.
		 *
		 * Performs the complete server startup sequence:
		 * 1. Analyzes configuration to identify unique (host, port) combinations
		 * 2. Creates listening sockets for each unique address using createListenSocketRaw()
		 * 3. Wraps sockets in Listener objects with RAII management
		 * 4. Registers all listeners with the event loop for connection monitoring
		 * 5. Builds hostname-to-virtual-server routing maps for request dispatch
		 *
		 * @throws std::runtime_error If any listening socket creation fails (DNS, bind, listen errors).
		 * @throws std::bad_alloc If memory allocation fails during listener creation or map building.
		 * @throws std::exception May propagate exceptions from EventLoop registration.
		 * 
		 * @warning Server must be in stopped state. Calling start() on running server is undefined.
		 * @warning If start() throws, the server remains in stopped state with partial cleanup.
		 * @note Uses exception-safe construction: temporary listeners are only published on full success.
		 * @note After successful start(), the server is ready to accept HTTP connections.
		 */
		void start();
		
		/**
		 * @brief Stops the server and releases all network resources.
		 *
		 * Performs graceful shutdown by:
		 * 1. Unregistering all listeners from the event loop
		 * 2. Closing all listening sockets via RAII cleanup
		 * 3. Clearing internal data structures
		 *
		 * Safe to call multiple times. No-op if server is already stopped.
		 * Does not affect existing client connections managed elsewhere.
		 *
		 * @note Does not throw exceptions. All operations are exception-safe.
		 * @note Server can be restarted by calling start() again after stop().
		 * @note Existing client connections are not affected by server stop.
		 */
		void stop();

		void run(int poll_timeout_ms);
		/**
		 * @brief Sets a file descriptor to non-blocking mode.
		 *
		 * Configures the given file descriptor to perform non-blocking I/O operations
		 * by setting the O_NONBLOCK flag using fcntl().
		 *
		 * @param fd The file descriptor to configure. Must be a valid open file descriptor.
		 * 
		 * @throws std::runtime_error If fcntl(F_GETFL) fails with errno details.
		 * @throws std::runtime_error If fcntl(F_SETFL) fails with errno details.
		 * 
		 * @warning Does not validate that fd is a valid file descriptor.
		 * @note Essential for event-driven I/O to prevent blocking operations.
		 */
		void setNonBlocking(int fd);
		
		/**
		 * @brief Sets the close-on-exec flag for a file descriptor.
		 *
		 * Ensures the file descriptor is automatically closed when the process
		 * executes another program (exec family functions). This prevents file
		 * descriptor leakage to child processes.
		 *
		 * @param fd The file descriptor to configure. Must be a valid open file descriptor.
		 * 
		 * @throws std::runtime_error If fcntl(F_GETFD) fails with errno details.
		 * @throws std::runtime_error If fcntl(F_SETFD) fails with errno details.
		 * 
		 * @warning Does not validate that fd is a valid file descriptor.
		 * @note Critical for security to prevent fd leakage to CGI processes.
		 */
		void setCloseOnExec(int fd);

		int resolveVirtualServerByPort(int localPort, const std::string& hostHdr) const;

		const ServerConfig&	getConfig()const;

		#ifdef UNIT_TEST
		public:
			size_t listenerCount() const { return listeners.size(); }
			int    listenerFD(size_t i) const { return (i < listeners.size() && listeners[i]) ? listeners[i]->getFD() : -1; }
			int    listenerPortAt(size_t i) const { return (i < listeners.size() && listeners[i]) ? listeners[i]->getPort() : -1; }
		#endif
};

class ClientHandler : public EventLoop::Handler {
	public:
		ClientHandler(EventLoop& loop, ClientConnection* c)
		: eventLoop(loop), clientConnection(c) {}

		virtual ~ClientHandler() { delete clientConnection; }

		virtual void onEvent(int fd, short revents) {
			// error/hangup → close
			if (revents & (POLLERR | POLLHUP | POLLNVAL)) {
				clientConnection->close();
				eventLoop.removeFD(fd); // deletes this
				return;
			}
			if (revents & POLLIN) {
				clientConnection->onReadable();
				if (clientConnection->isClosed()) { eventLoop.removeFD(fd); return; }
				if (clientConnection->wantsWrite()) eventLoop.modFD(fd, POLLOUT);
			}
			if (revents & POLLOUT) {
				clientConnection->onWritable();
				if (clientConnection->isClosed()) { eventLoop.removeFD(fd); return; }
				if (!clientConnection->wantsWrite()) eventLoop.modFD(fd, POLLIN);
			}
		}
	private:
		EventLoop& eventLoop;
		ClientConnection* clientConnection; // owned
};


class AcceptorHandler : public EventLoop::Handler {
	public:
		AcceptorHandler(EventLoop& loop, Server& srv, Listener* L)
		: eventLoop(loop), _srv(srv), listener(L) {}

		virtual void onEvent(int fd, short revents) {
			if (!listener || listener->getFD() != fd) return;

			if (revents & (POLLERR | POLLHUP | POLLNVAL)) {
				eventLoop.removeFD(fd); // stop accepting on this listener
				return;
			}

			if (revents & POLLIN) {
				for (;;) {
					struct sockaddr_storage ss;
					socklen_t sl = sizeof(ss);
					int cfd = ::accept(fd, (struct sockaddr*)&ss, &sl);
					if (cfd == -1) {
						if (errno == EINTR) continue;
						if (errno == EAGAIN || errno == EWOULDBLOCK) break;
						break; // other errors: give up this turn
					}
					// configure client
					try { _srv.setNonBlocking(cfd); _srv.setCloseOnExec(cfd); }
					catch (...) { ::close(cfd); continue; }

					// register client handler
					ClientConnection* c = new ClientConnection(cfd,&_srv);
					eventLoop.addFD(cfd, POLLIN, new ClientHandler(eventLoop, c));
				}
			}
		}
	private:
		EventLoop& eventLoop;
		Server& _srv;
		Listener* listener; // not owned
};




#endif // SERVER_H


