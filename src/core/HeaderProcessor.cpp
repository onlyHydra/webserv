#include "HeaderProcessor.h"
#include <cctype>
#include <cstdlib>

static std::string to_lower_copy(const std::string &in)
{
	std::string out(in);
	for (std::string::size_type i = 0; i < out.size(); ++i)
		out[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(out[i])));
	return out;
}

static std::string trim_copy(const std::string &in)
{
	std::string::size_type a = 0, b = in.size();
	while (a < b && std::isspace(static_cast<unsigned char>(in[a])))
		++a;
	while (b > a && std::isspace(static_cast<unsigned char>(in[b - 1])))
		--b;
	return in.substr(a, b - a);
}

static bool parse_size_decimal(const std::string &s, std::size_t &out)
{
	if (s.empty())
		return false;
	std::size_t i = 0;
	for (; i < s.size(); ++i)
	{
		unsigned char c = static_cast<unsigned char>(s[i]);
		if (c < '0' || c > '9')
			return false;
	}
	char *endp = 0;
	unsigned long v = std::strtoul(s.c_str(), &endp, 10);
	if (!endp || *endp != '\0')
		return false;
	out = static_cast<std::size_t>(v);
	return true;
}

static void setFail(int errorcode, std::string message, HeaderCheck &hc)
{
	hc.ok = false;
	hc.error_status = errorcode;
	hc.message = message;
}

HeaderCheck HeaderProcessor::analyze(const HttpRequest &req,
									 const Headers &hdrs,
									 std::size_t max_body_bytes_global)
{
	HeaderCheck hc;
	(void)max_body_bytes_global;
	// -------- Host required for HTTP/1.1 --------
	if (req.getHttpVer() == "HTTP/1.1")
	{
		const std::string &host = hdrs.get("Host");
		if (host.empty() || trim_copy(host).empty())
		{
			setFail(400, "Bad Request", hc);
			return hc;
		}
	}

	// -------- Transfer-Encoding: chunked? --------
	const std::string &te = hdrs.get("Transfer-Encoding");
	if (!te.empty())
	{
		std::string te_l = to_lower_copy(te);
		// Per RFC, a TE can be a comma-separated list;only support "chunked"
		if (te_l.find("chunked") != std::string::npos)
		{
			hc.chunked = true;
		}
		else
		{
			setFail(400, "Bad Request", hc);
			return hc;
		}
	}

	// -------- Content-Length parsing --------
	const std::string &cl = hdrs.get("Content-Length");
	if (!cl.empty())
	{
		std::string cl_trim = trim_copy(cl);
		std::size_t n = 0;
		if (!parse_size_decimal(cl_trim, n))
		{
			setFail(400, "Bad Request", hc);
			return hc;
		}
		hc.content_length = n;
	}

	// -------- TE vs CL mutual exclusivity --------
	if (hc.chunked && hc.content_length > 0)
	{
		setFail(400, "Bad Request", hc);
		return hc;
	}

	// -------- Global body cap--------
	// if (max_body_bytes_global && hc.content_length > 0 &&
	// 	hc.content_length > max_body_bytes_global)
	// {
	// 	hc.ok = false;
	// 	hc.error_status = "413 Payload Too Large";
	// 	return hc;
	// }

	// -------- Expect: 100-continue --------
	const std::string &ex = hdrs.get("Expect");
	if (!ex.empty())
	{
		std::string e = to_lower_copy(trim_copy(ex));
		if (e == "100-continue")
			hc.expect_continue = true;
		else
		{
			setFail(400, "Bad Request", hc);
			return hc;
		}
	}

	return hc;
}