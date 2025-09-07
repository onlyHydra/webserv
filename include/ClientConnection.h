#if !defined(CLIENTCONNECTION_H)
#define CLIENTCONNECTION_H

#include <string>
#include "ConnectionIO.h"
#include "HeaderProcessor.h"
#include "RequestGuards.h"
#include "PhaseDeadline.h"
#include "CGIStreamer.h"
#include "BodyReader.h"
#include "HttpRequest.h"
#include "HttpResponse.h"
#include "ResponseFactory.h"
#include "RequestContext.h"
#include "RouteResolver.h"
#include "Phase.h"
#include "RoutePlan.h"
// #include "RequestDecoder.h"

class Server;

class ClientConnection
{
	public:
		ClientConnection(int fd, Server *s, unsigned long long nowMs);
		~ClientConnection();

		void onTick(unsigned long long now_ms);
		bool isClosed() const;
		bool wantsRead();
		bool isReadPaused();
		bool hasPendingWrite() const;
		int fd() const { 
			return io.getFD(); 
		}

		CGIStreamer &getCGIStreamer() { 
			return cgi; 
		}

		Phase getState() { 
			return state; 
		}
		FlowControl &getFlow() { 
			return io.getFlow(); 
		}
		void resetDeadline(int ms){	
			dl.reset(now_cached_ms, ms);
		};

		bool isReadyToClose() { 
			return ready_to_close; 
		}

		void close();

		int getFD() {
			return io.getFD();
		}
		
		bool pumpCgiToSocket(std::size_t max_bytes = 128u * 1024u);
		void drainRingIntoBody();
		
		
	private:
		void parseHeaders();
		void selectRouteOnce();
		void runPreflight();
		void decideBodyReader();
		void decideBodyReader(std::size_t content_lenght);
		void readBody();
		void routeAndBuild();
		void finishWriteOrNext();
		void fail(int code, const char *reason);
 		
		Phase state;
		Server *server;
		ConnectionIO io;
		HttpRequest req;
		HttpResponse res;
		IBodyReader *body;
		CGIStreamer cgi;
		PhaseDeadline dl;
		std::size_t hdr_bytes;
		std::size_t max_hdr_bytes;
		std::size_t max_body_bytes;
		bool should_close;
		bool route_selected;
		RoutePlan plan;
		int local_port;
		int vs_idx;

		RouteDecision *ctx;
		Preflight pr;

		std::size_t body_bytes_prev; // last observed body->bytes_received()
		int body_no_progress_ticks;
		int flush_no_progress_ticks;
		unsigned long long now_cached_ms;
		bool ready_to_close;
		std::size_t fixed_body_target_;  

		size_t        body_expected_;     // from Content-Length
		size_t        body_received_;     // progresses to body_expected_
		int           body_fd_;           // -1 if using RAM
		std::string   body_path_;         // temp file path if body_fd_ >= 0
		// in ClientConnection.h
		bool cgi_error_latched;

		static const size_t MEM_BODY_LIMIT = 32u * 1024u; // spill threshold
		

		enum
		{
			BODY_STALL_TICK_LIMIT = 30,
			FLUSH_STALL_TICK_LIMIT = 30
		};
};

#endif //  CLIENTCONNECTION_H
