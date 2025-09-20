	// src/http/handlers/UploadHandler.h
	#ifndef UPLOAD_HANDLER_H
	#define UPLOAD_HANDLER_H

	#include "Handler.h"
	#include "MultipartReader.h"
	#include <string>
	#include <vector>



	class UploadHandler : public Handler, public IMultipartSink {
	public:
		UploadHandler();
		virtual ~UploadHandler();

		virtual bool handle(HttpRequest& req, HttpResponse& res, RequestContext& ctx);

		// IMultipartSink
		virtual bool onPartBegin(const Part& p);
		virtual bool onPartData(const char* data, std::size_t n);
		virtual bool onPartEnd();

	private:
		// config binding per request
		std::string upload_dir_;
		bool        overwrite_;
		std::size_t per_part_cap_;

		// per-part state
		std::string cur_name_;
		std::string cur_filename_raw_;
		std::string cur_filename_sanit_;
		std::string cur_tmp_path_;
		int         cur_fd_;
		std::size_t cur_written_;
		int         last_error_code_;
		std::string last_error_msg_;

		// helpers
		std::string sanitizeFilename(const std::string& in) const;
		bool ensureUploadDirIsSafe(const std::string& base) const;
		bool openTempInUpload(std::string& out_path, int& out_fd);
		bool finishMoveAtomic();

		// for building a JSON-ish response of saved files (optional)
		std::vector<std::string> saved_;
	};

	#endif
