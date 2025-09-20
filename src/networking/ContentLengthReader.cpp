#include "ContentLenghtReader.h"

ContentLenghtReader::ContentLenghtReader(std::size_t total_len, HttpRequest &req)
: need_(total_len), got_(0), done_(total_len == 0), req_(req)
{
	// nothing else to do
}

std::size_t ContentLenghtReader::consume(const char *data, std::size_t len)
{
	if (done_ || got_ >= need_) { 
		done_ = true; 
		return 0; }

	const std::size_t remaining = need_ - got_;
	const std::size_t take = (len < remaining) ? len : remaining;

	// You should really check if appendBody returns false 
	// -> meaning the ContentLength was exceeeded

	if (take) {
		req_.appendBody(data, take);  // write straight into HttpRequest::body
		got_ += take;
	}
	if (got_ >= need_)
		done_ = true;
	return take;
}
