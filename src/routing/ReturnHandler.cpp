/* --- PutPatchHandler.cpp --- */

/* ------------------------------------------
author: me
date: 8/16/2025
------------------------------------------ */

#include "ReturnHandler.h"

std::string ReturnHandler::handle(int code, const std::string &option)
{
	HttpResponse res;
	res.ensureDefaultHeaders();
	res.setStatus(code);
	if (code == HTTP_OK)
	{
		res.headers.set(HDR_CONTENT_TYPE, "text/plain; charset=utf-8");
		res.setBody(option);
	}
	else if (code == HTTP_MOVED_PERM || \
			code == HTTP_FOUND || \
			code == HTTP_TEMP_REDIR || \
			code == HTTP_PERM_REDIR)
	{
		res.headers.set(HDR_LOCATION, option);
	}
	std::ostringstream os;
	os << res;
	return (os.str());
}
