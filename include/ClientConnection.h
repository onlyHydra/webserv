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
#include "CgiProcess.h" 
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

struct CgiState {
  CgiProcess proc;
  int in_fd;          // proc.inFD()
  int out_fd;         // proc.outFD()
  size_t body_off;
  std::string buf;    // accumulates CGI output
  bool headers_done;
  int status;         // parsed "Status: NNN" or 200
  long content_len;   // from CGI headers, or -1 if unknown
  unsigned long long deadline;
};


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
		
		size_t cgi_bytes_streamed;   // bytes pushed into outBuffer after headers
		bool   cgi_headers_emitted;  // true once HTTP headers were sent to client


		// ---- I/O limits ----
		static const size_t READ_CHUNK   = 8192;
		// Protect against slowloris / memory blowups:
		static const size_t MAX_INBUFFER = (1u << 20); // 1 MiB
		// Cap CGI output to something reasonable (16 MiB here)
		static const size_t CGI_MAX_OUTPUT = (16u << 20);   // 16 MiB



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

		
		bool processIncoming();
		#ifdef UNIT_TEST
    		void ensureHelloInBuffer_();  // test-only safety net for POLLOUT-driven tests
		#endif
		
		// bool headersComplete(const std::vector<char> &buf, size_t &parseOffset, HttpRequest &request);

	public:

	  // --- CGI state (minimal, safe defaults) ---
	  
	  
	  static const int HDR_TIMEOUT_MS   = 10000; // headers read deadline
	  static const int BODY_TIMEOUT_MS  = 20000; // (reserved for future body reads)
	  static const int WRITE_TIMEOUT_MS = 10000; // response flush deadline
	  // near your other timeouts
	  static const unsigned POST_WRITE_LINGER_MS = 100; // close very soon after flush
	  
	  
	  // ---- backpressure watermarks (bytes) ----
	  static const size_t HIGH_WATER = 256u * 1024u; // pause reads if above
	  static const size_t LOW_WATER  =  64u * 1024u; // resume reads if below
	  
	  CgiProcess proc;
	  bool        cgi_active;
	  int         cgi_in_fd;
	  int         cgi_out_fd;
	  size_t      cgi_body_off;
	  std::string cgi_buf;
	  bool        cgi_headers_done;
	  int         cgi_status;
	  long        cgi_content_len;
	  unsigned long long cgi_deadline;

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
			res(),
			cgi_active(false),
			cgi_in_fd(-1),
			cgi_out_fd(-1),
			cgi_body_off(0),
			cgi_headers_done(false),
			cgi_status(200),
			cgi_content_len(-1),
			cgi_deadline(0ULL)
			{}
	
		explicit ClientConnection(int fd,const Server* srv) : state(READ_HEADERS), fd(fd),
			inBuffer(), outBuffer(),
			parseOffset(0), outOffset(0),
			server(srv), vs_idx(-1),
			deadline_ms(nowMs() + HDR_TIMEOUT_MS),
			readPaused(false),
			writeLingerArmed(false),
			req(),
			res(),
			cgi_active(false),
			cgi_in_fd(-1),
			cgi_out_fd(-1),
			cgi_body_off(0),
			cgi_headers_done(false),
			cgi_status(200),
			cgi_content_len(-1),
			cgi_deadline(0ULL)
			{}
		~ClientConnection() {};

		bool makeHelloResponse(); 

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
		
		State getState() { return this->state; }
		
		void onReadable();
		
		void onWritable();
		
		void changeState(State);
		
		
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

		bool beginCgi(const CgiSpec& spec,
              const std::string& script_path,
              const std::vector<std::string>& envv);
		void unregisterCgiFds();

		// Called by the EventLoop when a registered CGI fd is readable/writable.
		void onCgiReadable(int fd);
		void onCgiWritable(int fd);

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
