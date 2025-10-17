// src/http/handlers/UploadHandler.cpp
#include "UploadHandler.h"
#include "RequestContext.h"
#include "VirtualServer.h"
#include "ResponseFactory.h"
#include "HEADER_ENTRIES.h"
#include "Util.h"
#include "PathUtil.h"

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
#include <iostream>


static bool realpathString(const std::string& in, std::string& out) {
	char tmp[4096];
	if (Util::realpath(in.c_str(), tmp) == 0) return false;
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
    const std::string dir = upload_dir_;
    const std::string prefix = "upload";
    const int MAX_ATTEMPTS = 256;
    unsigned int salt = (unsigned int)std::rand(); 

    for (int i = 0; i < MAX_ATTEMPTS; ++i) {
        std::ostringstream name;
        name << (dir.empty() ? "." : dir);
        if (!dir.empty() && dir[dir.size() - 1] != '/')
            name << '/';
        name << (prefix.empty() ? "upload" : prefix)
             << "-tmp-"
             << salt << "-" << i;

        const std::string path = name.str();

        int fd = ::open(path.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
        if (fd >= 0) {
            (void)fcntl(fd, F_SETFD, fcntl(fd, F_GETFD, 0) | FD_CLOEXEC);
            out_path = path;
            out_fd = fd;
            return true;
        }
    }
    return false;
}



// UploadHandler.cpp
bool UploadHandler::finishMoveAtomic() {
	if (cur_fd_ < 0) return false;
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
	if (std::rename(cur_tmp_path_.c_str(), dst.c_str()) != 0) {
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

    if (!openTempInUpload(cur_tmp_path_, cur_fd_)) {
        last_error_code_ = 500;
        last_error_msg_  = "temporary file open failed";
        return false;
    }
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
bool UploadHandler::handle(HttpRequest &req, HttpResponse &res, RequestContext &ctx)
{
    // ---------- helpers ----------
    struct Err {
        static bool send(HttpResponse &res, int code, const std::string &msg, bool close)
        {
            std::string text = msg + "\n";
            res.status = code;
            if (code == 415)      res.reason = "Unsupported Media Type";
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
    struct Sniffer {
        static std::string sniffFromMem(const std::vector<char> &v)
        {
            if (v.size() < 6) return std::string(); // minimal "--x\r\n"
            std::size_t i = 0, n = v.size();
            while (i < n && v[i] != '\n' && !(i + 1 < n && v[i] == '\r' && v[i + 1] == '\n')) ++i;
            if (i == 0 || i >= n) return std::string();
            std::string line(&v[0], &v[0] + i);
            if (line.size() >= 3 && line[0] == '-' && line[1] == '-') {
                std::string b = line.substr(2);
                while (!b.empty() && (b[b.size()-1] == ' ' || b[b.size()-1] == '\t')) b.erase(b.size()-1);
                while (!b.empty() && (b[0] == ' ' || b[0] == '\t')) b.erase(0, 1);
                if (!b.empty()) return b;
            }
            return std::string();
        }
        static std::string sniffFromFile(const std::string &path)
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

    // ---------- per-request reset ----------
    saved_.clear();
    last_error_code_ = 0;
    last_error_msg_.clear();
    if (cur_fd_ >= 0) { ::close(cur_fd_); cur_fd_ = -1; }

    // ---------- config ----------
    const Location *L = ctx.loc;
    if (!L) return Err::send(res, 500, "No location", true);

    upload_dir_    = L->upload_store;
    overwrite_     = L->upload_overwrite;
    per_part_cap_  = L->upload_max_file_size;

    if (upload_dir_.empty())
        return Err::send(res, 501, "Uploads disabled (upload_store empty)", true);
    if (!ensureUploadDirIsSafe(upload_dir_))
        return Err::send(res, 500, "Bad upload_store (missing or not a directory)", true);

    // ---------- content-type (prefer header; robust fallback to sniff) ----------
// ---------- content-type (robust) ----------
    std::string ctype = req.getHeaders().get("Content-Type");
    std::string body_file = req.getBodyFilePath();
    std::vector<char> mem;
    if (body_file.empty())
        mem = req.readBodyToVector();

    // debug: what did we get?
    std::fprintf(stderr, "[UPLOAD] Raw Content-Type: '%s' (body_file=%s, mem=%lu)\n",
                ctype.c_str(), body_file.empty() ? "mem" : body_file.c_str(),
                (unsigned long)mem.size());

    // small helpers
    struct Str {
        static std::string trim(const std::string &s){
            std::string r = s;
            while(!r.empty() && (r[0]==' '||r[0]=='\t')) r.erase(0,1);
            while(!r.empty() && (r[r.size()-1]==' '||r[r.size()-1]=='\t')) r.erase(r.size()-1);
            return r;
        }
        static void tolower(std::string &s){
            for (size_t i=0;i<s.size();++i) if (s[i]>='A'&&s[i]<='Z') s[i]=char(s[i]-'A'+'a');
        }
    };

    // Parse media type and boundary (accept quotes, extra params, odd spacing)
    std::string mainType;
    std::string boundaryParam;

    if (!ctype.empty()) {
        std::string t = ctype;
        // split at ';'
        std::string::size_type sc = t.find(';');
        std::string mt = (sc==std::string::npos) ? t : t.substr(0, sc);
        Str::tolower(mt);
        mainType = Str::trim(mt);

        // scan params for boundary=
        std::string params = (sc==std::string::npos) ? "" : t.substr(sc+1);
        // tokenize params by ';'
        std::string::size_type pos = 0;
        while (pos < params.size()) {
            std::string::size_type next = params.find(';', pos);
            std::string part = (next==std::string::npos) ? params.substr(pos) : params.substr(pos, next-pos);
            part = Str::trim(part);
            // case-insensitive compare startswith "boundary="
            if (part.size() >= 9) {
                std::string key = part.substr(0, 8); Str::tolower(key);
                if (key == "boundary") {
                    // support "boundary = value" and "boundary=value"
                    std::string::size_type eq = part.find('=');
                    if (eq != std::string::npos) {
                        std::string val = Str::trim(part.substr(eq+1));
                        // strip optional quotes
                        if (!val.empty() && (val[0]=='"' || val[0]=='\'')) {
                            char q = val[0];
                            if (val.size()>=2 && val[val.size()-1]==q) val = val.substr(1, val.size()-2);
                        }
                        boundaryParam = val;
                    }
                }
            }
            if (next==std::string::npos) break;
            pos = next+1;
        }
    }

    // If header missing or no usable boundary → sniff and synthesize
    if (ctype.empty() || mainType != "multipart/form-data" || boundaryParam.empty()) {
        // read start of body (mem or first 4096 of file) and sniff
        std::string sniffed = !body_file.empty()
                                ? Sniffer::sniffFromFile(body_file)
                                : Sniffer::sniffFromMem(mem);
        if (!sniffed.empty()) {
            mainType = "multipart/form-data";
            boundaryParam = sniffed;
            ctype = "multipart/form-data; boundary=" + sniffed;
        }
    }

    // Hard check on type
    if (mainType != "multipart/form-data") {
        std::fprintf(stderr, "[UPLOAD] Reject: mainType='%s'\n", mainType.c_str());
        return Err::send(res, 415, "multipart/form-data required", false);
    }

    // Initialize parser; retry once if it fails
    MultipartReader mp;
    bool init_ok = mp.initFromContentType(ctype);
    if (!init_ok) {
        // as a last resort, rebuild from sniff even if we had a boundary
        std::string sniffed = !body_file.empty()
                                ? Sniffer::sniffFromFile(body_file)
                                : Sniffer::sniffFromMem(mem);
        if (!sniffed.empty()) {
            std::string rebuilt = "multipart/form-data; boundary=" + sniffed;
            if (rebuilt != ctype) {
                ctype = rebuilt;
                init_ok = mp.initFromContentType(ctype);
            }
        }
    }
    if (!init_ok) {
        std::fprintf(stderr, "[UPLOAD] Reject: initFromContentType failed. ctype='%s'\n", ctype.c_str());
        return Err::send(res, 415, "multipart/form-data required (bad or missing boundary)", false);
    }

    if (body_file.empty() && mem.empty())
        return Err::send(res, 400, "Body unavailable (no temp file, no memory body)", true);


    // ---------- feed body to parser ----------
    const std::size_t CHUNK = 64 * 1024;
    if (!body_file.empty()) {
        int fd = ::open(body_file.c_str(), O_RDONLY);
        if (fd < 0) {
            std::ostringstream m; m << "Body file open failed: " << body_file << " errno=" << errno;
            return Err::send(res, 400, m.str(), true);
        }
        std::vector<char> buf; buf.resize(CHUNK);
        for (;;) {
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
    } else {
        if (!mem.empty()) mp.feed(&mem[0], mem.size(), this);
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
    std::ostringstream ok; ok << "Uploaded:";
    for (size_t i = 0; i < saved_.size(); ++i) ok << (i ? ", " : " ") << saved_[i];
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

