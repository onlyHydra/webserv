// src/http/handlers/UploadHandler.cpp
#include "UploadHandler.h"
#include "RequestContext.h"
#include "VirtualServer.h"
#include "ResponseFactory.h"
#include "HEADER_ENTRIES.h"

#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <vector>
#include <stdlib.h>   // realpath
#include <limits.h>   // PATH_MAX
#include <unistd.h>   // mkstemp, close
#include <iostream>


static bool realpathString(const std::string& in, std::string& out) {
	char tmp[4096];
	if (::realpath(in.c_str(), tmp) == 0) return false;
	out.assign(tmp);
	return true;
}

UploadHandler::UploadHandler() : cur_fd_(-1), cur_written_(0), last_error_code_(0) {}

UploadHandler::~UploadHandler() { if (cur_fd_>=0) ::close(cur_fd_); }

std::string UploadHandler::sanitizeFilename(const std::string& in) const {
	// drop directories, keep basename
	std::string name = in;
	for (std::size_t i=0;i<name.size();++i) if (name[i]=='\\') name[i]='/';
	std::string::size_type s = name.rfind('/');
	if (s != std::string::npos) name = name.substr(s+1);

	// reject dotdot
	if (name == "." || name == "..") name.clear();

	// allow [A-Za-z0-9._-], replace others with '_'
	std::string out; out.reserve(name.size());
	for (std::size_t i=0;i<name.size();++i) {
		char c = name[i];
		bool ok =
			(c>='A'&&c<='Z')||(c>='a'&&c<='z')||
			(c>='0'&&c<='9')|| c=='.'||c=='_'||c=='-';
		out.push_back(ok?c:'_');
	}
	if (out.empty()) out = "file";
	if (out.size() > 128) out = out.substr(0,128);
	return out;
}

bool UploadHandler::ensureUploadDirIsSafe(const std::string& base) const {
	// ensure realpath(upload_dir) exists and is a directory
	std::string canon;
	if (!realpathString(base, canon)) return false;
	struct stat st; if (::stat(canon.c_str(), &st)!=0 || !S_ISDIR(st.st_mode)) return false;
	return true;
}

bool UploadHandler::openTempInUpload(std::string& out_path, int& out_fd) {
	// create temp file in upload_dir for atomic rename within same fs
	std::string templ = upload_dir_;
	if (!templ.empty() && templ[templ.size()-1] != '/') templ += '/';
	templ += ".upload-XXXXXX";
	std::vector<char> buf(templ.begin(), templ.end());
	buf.push_back('\0');
	int fd = ::mkstemp(&buf[0]);
	if (fd < 0) return false;
	::fcntl(fd, F_SETFD, FD_CLOEXEC);
	out_path.assign(&buf[0]);
	out_fd = fd;
	return true;
}

// UploadHandler.cpp
bool UploadHandler::finishMoveAtomic() {
	if (cur_fd_ < 0) return false;
	::fsync(cur_fd_);
	::close(cur_fd_);
	cur_fd_ = -1;

	std::string dst = upload_dir_;
	if (!dst.empty() && dst[dst.size()-1] != '/') dst += '/';
	dst += cur_filename_sanit_;

	if (!overwrite_) {
		struct stat st;
		if (::stat(dst.c_str(), &st) == 0) {
			last_error_code_ = 409;
			last_error_msg_  = "File exists: " + cur_filename_sanit_;
			return false; // will cause parser to stop with sink error
		}
	}
	if (::rename(cur_tmp_path_.c_str(), dst.c_str()) != 0) {
		last_error_code_ = 500;
		last_error_msg_  = "Rename failed";
		return false;
	}
	saved_.push_back(cur_filename_sanit_);
	cur_tmp_path_.clear();
	return true;
}


// ---- IMultipartSink ----
bool UploadHandler::onPartBegin(const Part& p) {
	cur_name_          = p.name;
	cur_filename_raw_  = p.filename;
	cur_filename_sanit_= sanitizeFilename(cur_filename_raw_);
	cur_written_       = 0;
	cur_fd_            = -1;
	cur_tmp_path_.clear();

	if (!openTempInUpload(cur_tmp_path_, cur_fd_)) return false;
	return true;
}

bool UploadHandler::onPartData(const char* data, std::size_t n) {
	if (cur_fd_ < 0) return false;

	// per-part cap
	if (per_part_cap_ && cur_written_ + n > per_part_cap_) {
		last_error_code_ = 413;                  // <--- add
		last_error_msg_  = "part too large";     // <--- add
		return false; // sink refuses -> parser stops
	}

	std::size_t off = 0;
	while (off < n) {
		ssize_t w = ::write(cur_fd_, data + off, (n - off));
		if (w > 0) off += (std::size_t)w;
		else if (w < 0 && errno == EINTR) continue;
		else { last_error_code_ = 500; last_error_msg_ = "write failed"; return false; }
	}
	cur_written_ += n;
	return true;
}


bool UploadHandler::onPartEnd() {
	return finishMoveAtomic();
}

// ---- handler entry ----
bool UploadHandler::handle(HttpRequest& req, HttpResponse& res, RequestContext& ctx)
{
    // ---------- helpers ----------
    struct Err {
        static bool send(HttpResponse& res, int code, const std::string& msg, bool close)
        {
            std::string text = msg + "\n";
            res.status = code;
            if      (code == 415) res.reason = "Unsupported Media Type";
            else if (code == 409) res.reason = "Conflict";
            else if (code == 413) res.reason = "Payload Too Large";
            else if (code >= 500) res.reason = "Internal Server Error";
            else                  res.reason = "Bad Request";

            res.headers.set("Content-Type", "text/plain; charset=utf-8");
            std::ostringstream cl; cl << (unsigned long)text.size();
            res.headers.set("Content-Length", cl.str());
            res.body.assign(text.begin(), text.end());
            res.bodyLength = text.size();
            res.headers.set("Connection", close ? "close" : "keep-alive");
            res.headers.set("Server", "webserv/1.0");
            return true;
        }
    };

    // Sniff boundary from the beginning of a multipart body if Content-Type is missing.
    // We look for a first line like:  "--<boundary>\r\n"
    struct Sniffer {
        static std::string sniffFromMem(const std::vector<char>& v)
        {
            if (v.size() < 6) return std::string(); // minimal "--x\r\n"
            // find end of first line (CRLF or LF)
            std::size_t i = 0, n = v.size();
            while (i < n && v[i] != '\n' && !(i + 1 < n && v[i] == '\r' && v[i + 1] == '\n')) ++i;
            if (i == 0 || i >= n) return std::string();
            std::string line(&v[0], &v[0] + i);
            if (line.size() >= 3 && line[0] == '-' && line[1] == '-') {
                std::string b = line.substr(2);
                // trim simple spaces/tabs defensively
                while (!b.empty() && (b[b.size()-1] == ' ' || b[b.size()-1] == '\t')) b.erase(b.size()-1);
                while (!b.empty() && (b[0] == ' ' || b[0] == '\t')) b.erase(0,1);
                if (!b.empty()) return b;
            }
            return std::string();
        }
        static std::string sniffFromFile(const std::string& path)
        {
            int fd = ::open(path.c_str(), O_RDONLY);
            if (fd < 0) return std::string();
            char buf[4096];
            ssize_t r = ::read(fd, buf, (int)sizeof(buf));
            ::close(fd);
            if (r <= 0) return std::string();
            std::vector<char> v(buf, buf + r);
            return sniffFromMem(v);
        }
    };

    // ---------- debug: quick summary ----------
    // std::cerr << "[UPLOAD] disk=" << (req.isBodyOnDisk()?1:0)
    //           << " mem="  << (unsigned long)req.getBodyLength()
    //           << " CT(raw)=" << req.getHeaders().get("Content-Type")
    //           << std::endl;

    // ---------- per-request reset ----------
    saved_.clear();
    last_error_code_ = 0;
    last_error_msg_.clear();
    if (cur_fd_ >= 0) { ::close(cur_fd_); cur_fd_ = -1; }

    // ---------- config ----------
    const Location* L = ctx.loc;
    if (!L) {
        #if defined(DEBUG)
        std::fprintf(stderr, "[UPLOAD] no location\n");
        #endif
        return Err::send(res, 500, "No location", true);
    }
    upload_dir_   = L->upload_store;
    overwrite_    = L->upload_overwrite;
    per_part_cap_ = L->upload_max_file_size;

    #if defined(DEBUG)
    std::fprintf(stderr, "[UPLOAD] dir='%s' overwrite=%d per_part_cap=%lu\n",
                 upload_dir_.c_str(), overwrite_?1:0, (unsigned long)per_part_cap_);
    #endif

    if (upload_dir_.empty())
        return Err::send(res, 501, "Uploads disabled (upload_store empty)", true);
    if (!ensureUploadDirIsSafe(upload_dir_))
        return Err::send(res, 500, "Bad upload_store (missing or not a directory)", true);

    // ---------- content-type (prefer header; fallback to sniff) ----------
    std::string ctype = req.getHeaders().get("Content-Type");
    #if defined(DEBUG)
    std::fprintf(stderr, "[UPLOAD] Content-Type(raw): %s\n", ctype.c_str());
    #endif

    std::string body_file = req.getBodyFilePath();
    std::vector<char> mem;
    if (body_file.empty())
        mem = req.readBodyToVector();

    if (ctype.empty()) {
        std::string b = !body_file.empty()
                        ? Sniffer::sniffFromFile(body_file)
                        : Sniffer::sniffFromMem(mem);
        if (!b.empty()) {
            ctype = "multipart/form-data; boundary=" + b;
            #if defined(DEBUG)
            std::fprintf(stderr, "[UPLOAD] inferred Content-Type: %s\n", ctype.c_str());
            #endif
        }
    }

    // Validate multipart/form-data prefix (case-insensitive)
    bool ct_ok = false;
    if (!ctype.empty()) {
        const char* want = "multipart/form-data";
        // C++98: lowercase the prefix and compare
        std::string pref = ctype.substr(0, 19);
        for (std::size_t i = 0; i < pref.size(); ++i) {
            char ch = pref[i];
            if (ch >= 'A' && ch <= 'Z') pref[i] = char(ch - 'A' + 'a');
        }
        ct_ok = (pref == want);
    }
    if (!ct_ok)
        return Err::send(res, 415, "multipart/form-data required", false);

    // ---------- parse multipart ----------
    MultipartReader mp;
    if (!mp.initFromContentType(ctype)) {
        #if defined(DEBUG)
        std::fprintf(stderr, "[UPLOAD] Multipart init failed; CT='%s'\n", ctype.c_str());
        #endif
        return Err::send(res, 415, "multipart/form-data required", false);
    }

    #if defined(DEBUG)
    std::fprintf(stderr, "[UPLOAD] body_file='%s' mem_size=%lu\n",
        body_file.c_str(), (unsigned long)mem.size());
    #endif
    if (body_file.empty() && mem.empty())
        return Err::send(res, 400, "Body unavailable (no temp file, no memory body)", true);

    const std::size_t CHUNK = 64 * 1024;
    if (!body_file.empty())
    {
        int fd = ::open(body_file.c_str(), O_RDONLY);
        if (fd < 0) {
            std::ostringstream m; m << "Body file open failed: " << body_file << " errno=" << errno;
            return Err::send(res, 400, m.str(), true);
        }
        std::vector<char> buf; buf.resize(CHUNK);
        for (;;)
        {
            ssize_t r = ::read(fd, &buf[0], (int)buf.size());
            if (r == 0) break;
            if (r < 0) {
                if (errno == EINTR) continue;
                ::close(fd);
                return Err::send(res, 400, "Read error", true);
            }
            mp.feed(&buf[0], (std::size_t)r, this);
            if (mp.error()) { ::close(fd); break; }
        }
        ::close(fd);
    }
    else
    {
        if (!mem.empty())
            mp.feed(&mem[0], mem.size(), this);
    }

    // ---------- errors ----------
    if (mp.error()) {
        int code = last_error_code_ ? last_error_code_ : 400;
        std::string msg = last_error_msg_.empty()
            ? std::string("multipart parse error: ") + mp.errorMsg()
            : last_error_msg_;
        return Err::send(res, code, msg, (code >= 500));
    }
    if (!mp.done())
        return Err::send(res, 400, "Incomplete multipart (no closing boundary)", false);

    // ---------- success ----------
    std::ostringstream ok;
    ok << "Uploaded:";
    for (size_t i = 0; i < saved_.size(); ++i)
        ok << (i ? ", " : " ") << saved_[i];

    std::string text = ok.str() + "\n";
    res.status = saved_.empty() ? 200 : 201;
    res.reason = saved_.empty() ? "OK" : "Created";
    res.headers.set("Content-Type", "text/plain; charset=utf-8");
    std::ostringstream cl; cl << (unsigned long)text.size();
    res.headers.set("Content-Length", cl.str());
    res.body.assign(text.begin(), text.end());
    res.bodyLength = text.size();
    res.headers.set("Connection", "keep-alive");
    res.headers.set("Server", "webserv/1.0");
    return true;
}



