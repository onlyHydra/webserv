

#ifndef MULTIPART_READER_H
#define MULTIPART_READER_H

#include <string>
#include <map>
#include <vector>
#include <cstddef>

class IMultipartSink {
	public:
		virtual ~IMultipartSink() {}
		struct Part {
			std::string name;
			std::string filename;
			std::map<std::string,std::string> headers;
		};
		virtual bool onPartBegin(const Part& p) = 0;                  // open temp target, reset counters
		virtual bool onPartData(const char* data, std::size_t n) = 0; // write chunk, enforce caps
		virtual bool onPartEnd() = 0;                                  // fsync/close, atomic rename
};

class MultipartReader {
	public:
		MultipartReader();

		// Parse "multipart/form-data; boundary=----abc"
		bool initFromContentType(const std::string& contentType);
		bool setBoundary(const std::string& boundary); // alternative

		// Feed streaming data; returns bytes consumed (<= n)
		std::size_t feed(const char* data, std::size_t n, IMultipartSink* sink);

		// Terminal states
		bool done() const { return st_ == S_DONE; }
		bool error() const { return st_ == S_ERROR; }
		const std::string& errorMsg() const { return err_; }

	private:
		enum State { S_PREAMBLE, S_BOUNDARY, S_HEADERS, S_DATA, S_AFTER, S_DONE, S_ERROR };

		// Helpers
		static bool ieq(char a, char b);
		static std::string trim(const std::string& s);
		static bool parseContentDisposition(const std::string& v,
											std::string& name,
											std::string& filename);
		bool findLine(std::string& out); // extract next CRLF line from buf_
		bool startBoundaryLine(const std::string& line, bool& isFinal) const;

		void setError(const std::string& m) { st_ = S_ERROR; err_ = m; }

	private:
		State st_;
		std::string err_;

		std::string boundary_;      // e.g. "----WebKitFormBoundaryAbc"
		std::string dashBoundary_;  // "--" + boundary_
		std::string endBoundary_;   // "--" + boundary_ + "--"

		std::string buf_; // rolling buffer for lines / overlap

		// current part header set
		IMultipartSink::Part cur_;
};

#endif
