
#ifndef HANDLER_H
#define HANDLER_H

class HttpRequest;
class HttpResponse;

class Handler
{

	public:
		virtual ~Handler();
		virtual void handle(HttpRequest &req, HttpRequest &res) = 0;
};

#endif