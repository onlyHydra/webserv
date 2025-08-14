/* --- HttpRequest.cpp --- */

/* ------------------------------------------
author: undefined
date: 8/10/2025
------------------------------------------ */

#include "HttpRequest.h"

HttpRequest::HttpRequest()
{
}

HttpRequest::~HttpRequest()
{
}

bool	HttpRequest::keepAlive(void) const
{
}

bool	HttpRequest::headerAsSize(string k, size_t &v) const
{
}

string	HttpRequest::extension() const
{
}

bool	HttpRequest::parse(const char* data, size_t n)
{
}

string	HttpRequest::getMethod(void)
{
}

string	HttpRequest::getUri(void)
{
}

string	HttpRequest::getPath(void)
{
}

string	HttpRequest::getQuery(void)
{
}

string	HttpRequest::getSessId(void)
{
}

string	HttpRequest::getHttpVer(void)
{
}

string	HttpRequest::getHeaders(void)
{
}

string	HttpRequest::getCookies(void)
{
}

string	HttpRequest::getBody(void)
{
}
