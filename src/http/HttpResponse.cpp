/* --- HttpResponse.cpp --- */

/* ------------------------------------------
author: undefined
date: 8/10/2025
------------------------------------------ */

#include "HttpResponse.h"
#include <iostream>

HttpResponse::HttpResponse()
{
	this->http_version = "";
	this->session_id = "";
	this->bodyLength = 0;
}

HttpResponse::~HttpResponse()
{
}

std::ostream &operator<<(std::ostream &out, const HttpResponse &target)
{
	out << \
	target.http_version << " " << \
	target.session_id << " " << \
	target.bodyLength << std::endl << \
	"-----------" << std::endl;
	out << target.headers << std::endl << \
	"-----------" << std::endl;
	out.write(target.body.data(), target.body.size());
	out << std::endl << \
	"-----------";
	return (out);
}

