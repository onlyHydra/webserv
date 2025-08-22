/* --- ClientConnection.h --- */

/* ------------------------------------------
Author: undefined
Date: 8/10/2025
------------------------------------------ */

#ifndef CLIENTCONNECTION_H
#define CLIENTCONNECTION_H

#include <vector>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>
#include <string>
#include <sys/socket.h>

#include "UniqueFD.h"
#include "Router.h"
#include "RouteResolver.h"
#include "HttpRequest.h"
#include "HttpResponse.h"
class Server;

enum State
{
	READ_HEADERS,
	READ_BODY,
	WRITE,
	CLOSE,
};

/**
 * @class ClientConnection
 * @brief Manages a single client connection, handling socket I/O and state transitions.
 *
 * The ClientConnection class encapsulates the logic for reading from and writing to a client socket,
 * buffering incoming and outgoing data, and managing the connection's state throughout its lifecycle.
 * Currently implements a simple HTTP server that responds with a "hello" message to any complete request.
 *
 * @note This class is not copyable or movable.
 *
 * Members:
 * - State state: Current state of the connection (READ_HEADERS, WRITE, or CLOSE).
 * - UniqueFD fd: File descriptor wrapper for the client socket.
 * - std::vector<char> inBuffer: Buffer for incoming data from the client.
 * - std::vector<char> outBuffer: Buffer for outgoing data to the client.
 * - size_t parseOffset: Offset in the input buffer for incremental parsing (avoids rescanning).
 * - size_t outOffset: Offset in the output buffer for partial writes.
 *
 * Constants:
 * - static const size_t READ_CHUNK: Size of each read operation from the socket (8192 bytes).
 * - static const size_t MAX_INBUFFER: Maximum allowed size for the input buffer (1MB).
 *
 * Public Methods:
 * - explicit ClientConnection(int fd): Constructs a connection with the given socket file descriptor.
 * - ~ClientConnection(): Destructor.
 * - State getState(): Returns the current state of the connection.
 * - void onReadable(): Handles socket readability events by reading data and processing it.
 * - void onWritable(): Handles socket writability events by sending buffered data.
 * - void changeState(State): Changes the connection's state.
 * - void processIncoming(): Processes incoming data, looking for complete HTTP headers.
 * - void close(): Closes the client connection and transitions to CLOSE state.
 *
 * Private Methods:
 * - void readFromSocket(): Reads data from socket into inBuffer, respects MAX_INBUFFER limit.
 *
 * @warning After calling close(), the ClientConnection should be destroyed/removed.
 */


class ClientConnection
{
	private:
		enum State state;
		UniqueFD fd;
		std::vector<char> inBuffer;
		std::vector<char> outBuffer;
		size_t parseOffset;
		size_t outOffset;
		size_t bytesErased;
		const Server * server;
		int vs_idx;
		unsigned long long 	deadline_ms; // absolute deadline for current phase
		bool               	readPaused;  // if true, we should not register POLLIN
		bool 				writeLingerArmed; // after full flush, ask handler to keep POLLOUT once

		HttpRequest req;
		HttpResponse res;
		/**
		 * @brief Reads data from the client socket into inBuffer.
		 *
		 * This method performs non-blocking reads from the socket in READ_CHUNK
		 * sized pieces. It enforces the MAX_INBUFFER limit to prevent memory
		 * exhaustion attacks.
		 *
		 * Behavior:
		 * - Only reads when state is READ_HEADERS
		 * - Continues reading until socket would block (EAGAIN/EWOULDBLOCK)
		 * - Closes connection if buffer exceeds MAX_INBUFFER
		 * - Closes connection on read errors or EOF (n == 0)
		 * - Respects remaining buffer space to avoid overflow
		 */


		// ---- I/O limits ----
		static const size_t READ_CHUNK   = 8192;
		// Protect against slowloris / memory blowups:
		static const size_t MAX_INBUFFER = (1u << 20); // 1 MiB

		// ---- timeouts (milliseconds) ----
		// You can tweak these without touching code elsewhere.


		// ---- timing / backpressure bookkeeping ----
		// Use unsigned long long to stay C++98-friendly.


		// ---- helpers (implemented in .cpp) ----
		 static u_int64_t nowMs();

		inline void resetDeadlineForHeaders() { 
			deadline_ms = nowMs() + (unsigned long long)HDR_TIMEOUT_MS;
		}
		inline void resetDeadlineForBody()    { 
			deadline_ms = nowMs() + (unsigned long long)BODY_TIMEOUT_MS; 
		}
		inline void resetDeadlineForWrite()   { 
			deadline_ms = nowMs() + (unsigned long long)WRITE_TIMEOUT_MS;
		}
		inline void bumpDeadline(int ms)      { 
			deadline_ms = nowMs() + (unsigned long long)ms;
		}
		inline bool expired() const           { 
			return nowMs() > deadline_ms;
		}



		void readFromSocket();

		/* @brief Processes incoming data from the client connection.
		*
		* This method examines the input buffer for complete HTTP headers by searching
		* for the "\r\n\r\n" sequence. It uses parseOffset for incremental parsing to
		* avoid rescanning the entire buffer on each call.
		*
		* Current behavior (placeholder implementation):
		* - Only processes data when state is READ_HEADERS
		* - Searches for complete HTTP headers using helper function headersComplete()
		* - When headers are complete, generates a simple "hello" HTTP response
		* - Transitions to WRITE state to send the response
		*
		* @note This is a placeholder implementation that will be extended to proper
		*       HTTP request parsing later.
		*/
		bool processIncoming();
		
		// bool headersComplete(const std::vector<char> &buf, size_t &parseOffset, HttpRequest &request);

	public:

		static const int HDR_TIMEOUT_MS   = 10000; // headers read deadline
		static const int BODY_TIMEOUT_MS  = 20000; // (reserved for future body reads)
		static const int WRITE_TIMEOUT_MS = 10000; // response flush deadline
		// near your other timeouts
		static const unsigned POST_WRITE_LINGER_MS = 100; // close very soon after flush


		// ---- backpressure watermarks (bytes) ----
		static const size_t HIGH_WATER = 256u * 1024u; // pause reads if above
		static const size_t LOW_WATER  =  64u * 1024u; // resume reads if below


		bool wantsWriteLinger() const { return writeLingerArmed; }

		explicit ClientConnection(int fd_)
			: state(READ_HEADERS), fd(fd_),
			inBuffer(), outBuffer(),
			parseOffset(0), outOffset(0),
			server(0), vs_idx(-1),
			deadline_ms(nowMs() + HDR_TIMEOUT_MS),
			readPaused(false),
			writeLingerArmed(false),
			req(),
			res() {}
	
		explicit ClientConnection(int fd,const Server* srv) : state(READ_HEADERS), fd(fd),
			inBuffer(), outBuffer(),
			parseOffset(0), outOffset(0),
			server(srv), vs_idx(-1),
			deadline_ms(nowMs() + HDR_TIMEOUT_MS),
			readPaused(false),
			writeLingerArmed(false),
			req(),
			res() {}
		~ClientConnection() {};

		bool wantsRead() const {
			return state == READ_HEADERS && !isReadPaused();/*|| state == RECV_BODY*/;
		}
		bool hasPendingWrite() const {
			return outOffset < outBuffer.size();
		}
		bool isReadPaused() const {
			return readPaused;
		}
		void setReadPaused(bool v) {
			readPaused = v;
		}
		/**
		 * @brief Retrieves the current state of the client connection.
		 *
		 * @return The current State of the client connection.
		 */
		State getState() { return this->state; }
		/**
		 * @brief Handles the event when the client connection becomes readable.
		 *
		 * This method reads data from the client socket into the input buffer and then
		 * processes the incoming data to check for complete HTTP headers. It continues
		 * reading until the socket would block or an error occurs.
		 *
		 * The method:
		 * 1. Calls readFromSocket() to read available data
		 * 2. Calls processIncoming() to check for complete HTTP requests
		 */
		void onReadable();
		/**
		 * @brief Handles the event when the client connection becomes writable.
		 *
		 * This method sends buffered data from outBuffer to the client socket.
		 * It uses MSG_NOSIGNAL to avoid SIGPIPE and handles partial writes by
		 * updating outOffset. Once all data is sent, it closes the connection.
		 *
		 * The method:
		 * 1. Only operates when state is WRITE
		 * 2. Sends data starting from outOffset
		 * 3. Handles EAGAIN/EWOULDBLOCK by returning (to retry later)
		 * 4. Closes connection on errors or when all data is sent
		 */
		void onWritable();
		/**
		 * @brief Changes the current state of the client connection.
		 *
		 * This is a simple setter that updates the internal state variable.
		 * No validation or side effects are performed.
		 *
		 * @param state The new state to transition to (READ_HEADERS, WRITE, or CLOSE).
		 */
		void changeState(State);
		/**
		 * @brief Closes the client connection and releases any associated resources.
		 *
		 * This method safely closes the file descriptor using fd.reset() and
		 * transitions the connection state to CLOSE. It's safe to call multiple times.
		 *
		 * @warning The ClientConnection object should be destroyed/removed after
		 *          calling this method, as indicated by the implementation comment.
		 */
		
		void onTick();


		int  getFD() const { 
			return fd.get();
		}
		bool isClosed() const { 
			return !fd; 
		}
		bool wantsWrite() const {
			return state == WRITE;
		}
		
		void close();

		#ifdef UNIT_TEST
			public:
				std::vector<char>& getInBuffer() { return inBuffer; }
				std::vector<char>& getOutBuffer() { return outBuffer; }
				size_t& getParseOffset() { return parseOffset; }
				void setState(State state) {this->state = state;}
				bool processIncoming(std::string ok)
				{ 
					(void)ok;
					return this->processIncoming();
				};
				size_t getparseOffset(){return this->parseOffset;};
		#endif
};

#endif // CLIENTCONNECTION_H
