/* --- ProxyHandler.h --- */

/* ------------------------------------------
Author: undefined
Date: 8/16/2025
------------------------------------------ */

#ifndef PROXYHANDLER_H
#define PROXYHANDLER_H


#include "Handler.h"
#include "Util.h"
class ProxyHandler: public Handler {
	public:
		ProxyHandler();
		~ProxyHandler();
		bool handle(HttpRequest &req, HttpResponse &res, RequestContext &ctx);

	private:

};

#endif // PROXYHANDLER_H
