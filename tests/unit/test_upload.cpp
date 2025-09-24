// tests/unit/test_upload.cpp
#include "helpers/server_runner.hpp"
#include <catch2/catch_all.hpp>

#include <arpa/inet.h>
#include <dirent.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include <initializer_list>

// -----------------------------------------------------------------------------
// Config macros (override via -DUPLOAD_DIR_ON_DISK=\"/abs/path\")
// -----------------------------------------------------------------------------
#ifndef WEBSERV_CONF
#define WEBSERV_CONF() std::string()
#endif
#ifndef WEBSERV_BIN
#define WEBSERV_BIN() std::string()
#endif
#ifndef UPLOAD_URL
#define UPLOAD_URL() "/upload"
#endif
#ifndef UPLOAD_DIR_ON_DISK
#define UPLOAD_DIR_ON_DISK() std::string("./www/upload/files")
#endif

// -----------------------------------------------------------------------------
// Small utilities
// -----------------------------------------------------------------------------
template <typename T>
static bool is_one_of(const T& v, std::initializer_list<T> set) {
    for (typename std::initializer_list<T>::const_iterator it = set.begin(); it != set.end(); ++it)
        if (v == *it) return true;
    return false;
}

static std::string unique_name(const std::string& base, const std::string& ext) {
    static unsigned long counter = 0;
    unsigned long pid = static_cast<unsigned long>(::getpid());
    std::ostringstream oss;
    oss << base << "-" << pid << "-" << ++counter;
    if (!ext.empty()) { oss << "." << ext; }
    return oss.str();
}

static int connect_tcp_local(int port) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    REQUIRE(fd >= 0);

    sockaddr_in sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port   = htons(static_cast<uint16_t>(port));
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    int rc = ::connect(fd, reinterpret_cast<sockaddr*>(&sa), sizeof(sa));
    if (rc != 0) {
        INFO("connect() failed errno=" << errno << " (" << std::strerror(errno) << ")");
    }
    REQUIRE(rc == 0);
    return fd;
}

static void send_all(int fd, const std::string& data) {
    const char* p = data.data();
    size_t left = data.size();
    while (left) {
        ssize_t n = ::send(fd, p, left, 0);
        REQUIRE(n >= 0);
        p += n;
        left -= static_cast<size_t>(n);
    }
}

static std::string recv_all(int fd) {
    std::string out;
    char buf[4096];
    for (;;) {
        ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n == 0) break;
        REQUIRE(n >= 0);
        out.append(buf, buf + n);
        if (n < (ssize_t)sizeof(buf)) break;
    }
    return out;
}

// -----------------------------------------------------------------------------
// Minimal HTTP response parser
// -----------------------------------------------------------------------------
struct HttpResponse {
    int status = 0;
    std::string reason;
    std::map<std::string, std::string> headers;
    std::string body;
};

static HttpResponse parse_response(const std::string& raw) {
    HttpResponse r;

    const std::string::size_type pos = raw.find("\r\n\r\n");
    REQUIRE(pos != std::string::npos);
    const std::string head = raw.substr(0, pos);
    r.body = raw.substr(pos + 4);

    std::istringstream iss(head);
    std::string line;

    REQUIRE(std::getline(iss, line));
    if (!line.empty() && line[line.size()-1] == '\r') line.erase(line.size()-1);

    {
        std::istringstream sl(line);
        std::string httpver;
        sl >> httpver >> r.status;
        std::getline(sl, r.reason);
        if (!r.reason.empty() && r.reason[0] == ' ') r.reason.erase(0, 1);
    }

    while (std::getline(iss, line)) {
        if (!line.empty() && line[line.size()-1] == '\r') line.erase(line.size()-1);
        if (line.empty()) break;
        const std::string::size_type c = line.find(':');
        if (c == std::string::npos) continue;
        std::string k = line.substr(0, c);
        std::string v = line.substr(c + 1);
        while (!v.empty() && (v[0] == ' ' || v[0] == '\t')) v.erase(0, 1);
        r.headers[k] = v;
    }

    return r;
}

// -----------------------------------------------------------------------------
// Filesystem helpers
// -----------------------------------------------------------------------------
struct FileInfo { off_t size; time_t mtime; FileInfo():size(0),mtime(0){} };

static bool stat_regular(const std::string& path, FileInfo& fi) {
    struct stat st;
    if (::stat(path.c_str(), &st) != 0) return false;
    if (!S_ISREG(st.st_mode)) return false;
    fi.size = st.st_size;
    fi.mtime = st.st_mtime;
    return true;
}

static bool ensure_dir_exists(const std::string& dir) {
    if (dir.empty()) return false;

    std::string cur;
    size_t i = 0;
    if (!dir.empty() && dir[0] == '/') { cur = "/"; i = 1; }

    for (; i < dir.size(); ++i) {
        char ch = dir[i];
        cur.push_back(ch);
        if (ch == '/' || i + 1 == dir.size()) {
            if (!cur.empty() && cur != "/") {
                ::mkdir(cur.c_str(), 0755); // ignore EEXIST
            }
        }
    }
    struct stat st;
    return ::stat(dir.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

static std::map<std::string, FileInfo> snapshot_dir(const std::string& dir) {
    std::map<std::string, FileInfo> out;

    DIR* d = ::opendir(dir.c_str());
    if (!d) return out;

    for (;;) {
        errno = 0;
        dirent* de = ::readdir(d);
        if (!de) break;
        const char* name = de->d_name;
        if (std::strcmp(name, ".") == 0 || std::strcmp(name, "..") == 0) continue;

        std::string full = dir;
        if (!full.empty() && full[full.size()-1] != '/') full.push_back('/');
        full += name;

        FileInfo fi;
        if (stat_regular(full, fi)) out[full] = fi; // regular files only
    }
    ::closedir(d);
    return out;
}

static std::vector<std::string>
diff_new_files(const std::map<std::string, FileInfo>& before,
               const std::map<std::string, FileInfo>& after) {
    std::vector<std::string> added;
    for (std::map<std::string, FileInfo>::const_iterator it = after.begin(); it != after.end(); ++it) {
        const std::string& name = it->first;
        const FileInfo& fi = it->second;
        std::map<std::string, FileInfo>::const_iterator bit = before.find(name);
        if (bit == before.end()) {
            added.push_back(name);
        } else {
            const FileInfo& b = bit->second;
            if (fi.mtime > b.mtime || fi.size != b.size)
                added.push_back(name);
        }
    }
    return added;
}



// -----------------------------------------------------------------------------
// Multipart body builder
// -----------------------------------------------------------------------------
static std::string make_multipart_body(const std::string& boundary,
                                       const std::string& field_name,
                                       const std::string& filename,
                                       const std::string& payload,
                                       const std::string& ctype) {
    std::ostringstream oss;
    oss << "--" << boundary << "\r\n";
    oss << "Content-Disposition: form-data; name=\"" << field_name
        << "\"; filename=\"" << filename << "\"\r\n";
    oss << "Content-Type: " << ctype << "\r\n\r\n";
    oss << payload << "\r\n";
    oss << "--" << boundary << "--\r\n";
    return oss.str();
}



// --- tiny helpers used only by this file (C++14-safe) -----------------------
static bool is_one_of(int v, std::initializer_list<int> set) {
    for (int x : set) if (v == x) return true;
    return false;
}

static bool dir_exists(const std::string& path) {
    struct stat st;
    return ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}


static std::vector<std::string> list_dir_names(const std::string& dir) {
    std::vector<std::string> names;
    DIR* d = ::opendir(dir.c_str());
    if (!d) return names;
    while (true) {
        errno = 0;
        dirent* e = ::readdir(d);
        if (!e) break;
        std::string n = e->d_name;
        if (n == "." || n == "..") continue;
        names.push_back(n);
    }
    ::closedir(d);
    std::sort(names.begin(), names.end());
    return names;
}

static std::vector<std::string> diff_added(const std::vector<std::string>& before,
                                           const std::vector<std::string>& after) {
    std::vector<std::string> added;
    std::set_difference(after.begin(), after.end(),
                        before.begin(), before.end(),
                        std::back_inserter(added));
    return added;
}


// ---- helpers: keep ONE copy of these in the file ---------------------------
static inline bool read_file_to_string(const std::string& path, std::string& out) {
    std::ifstream ifs(path.c_str(), std::ios::in | std::ios::binary);
    if (!ifs) return false;
    std::ostringstream ss;
    ss << ifs.rdbuf();
    out = ss.str();
    return true;
}

static inline std::string join_path(const std::string& dir, const std::string& name) {
    if (dir.empty()) return name;
    if (dir[dir.size() - 1] == '/') return dir + name;
    return dir + "/" + name;
}

static inline bool file_has_payload(const std::string& dir,
                                    const std::string& name,
                                    const std::string& expected) {
    std::string content;
    if (!read_file_to_string(join_path(dir, name), content)) return false;
    return content == expected;
}

static inline bool ensure_dir_exists_bool(const std::string& path) {
    // Your original ensure_dir_exists may return void, so wrap it if needed
    // Replace this with your real implementation if you already have a bool-returning one.
    struct stat st;
    if (::stat(path.c_str(), &st) == 0) {
        return S_ISDIR(st.st_mode);
    }
    return ::mkdir(path.c_str(), 0755) == 0;
}



// ============================================================================
// TESTS
// ============================================================================
TEST_CASE("POST upload saves a file with the expected content", "[upload]") {
    ServerRunner server{WEBSERV_CONF(), WEBSERV_BIN()};

    const std::string upload_dir = UPLOAD_DIR_ON_DISK();
    REQUIRE(ensure_dir_exists(upload_dir));

    std::map<std::string, FileInfo> before = snapshot_dir(upload_dir);

    const std::string boundary = "----CatchBoundaryXyZ123456";
    const std::string field    = "file";
    const std::string fname    = unique_name("test_upload", "txt");   // unique per run
    const std::string payload  = "Hello from Catch2 upload test.\nLine 2.\n";
    const std::string ctype    = "text/plain";

    const std::string body = make_multipart_body(boundary, field, fname, payload, ctype);

    std::ostringstream req;
    req << "POST " << UPLOAD_URL() << " HTTP/1.1\r\n"
        << "Host: 127.0.0.1\r\n"
        << "User-Agent: catch2-upload-test\r\n"
        << "Accept: */*\r\n"
        << "Content-Type: multipart/form-data; boundary=" << boundary << "\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Connection: close\r\n\r\n"
        << body;

    int fd = connect_tcp_local(server.port);
    send_all(fd, req.str());
    HttpResponse r = parse_response(recv_all(fd));
    ::close(fd);

    INFO("Upload response: " << r.status << " " << r.reason);
    REQUIRE(is_one_of(r.status, {200, 201, 204, 303}));

    // REQUIRE(assert_uploaded(before, upload_dir, fname, payload));
    REQUIRE(file_has_payload(upload_dir, fname, payload));

}

TEST_CASE("POST upload can handle a second small file (idempotent/overwrite friendly)", "[upload][dup]") {
    ServerRunner server{WEBSERV_CONF(), WEBSERV_BIN()};

    const std::string upload_dir = UPLOAD_DIR_ON_DISK();
    REQUIRE(ensure_dir_exists_bool(upload_dir));

    // Use the SAME filename for both uploads
    const std::string fnameBase = unique_name("duplicate", "txt");
    const std::string fname = fnameBase;

    // -------- First upload --------
    const std::string boundary1 = "----CatchBoundaryFirstABC";
    const std::string field     = "file";
    const std::string payload1  = "payload-one\n";
    const std::string ctype     = "text/plain";
    const std::string body1     = make_multipart_body(boundary1, field, fname, payload1, ctype);

    std::ostringstream req1;
    req1 << "POST " << UPLOAD_URL() << " HTTP/1.1\r\n"
         << "Host: 127.0.0.1\r\n"
         << "User-Agent: catch2-upload-test\r\n"
         << "Accept: */*\r\n"
         << "Content-Type: multipart/form-data; boundary=" << boundary1 << "\r\n"
         << "Content-Length: " << body1.size() << "\r\n"
         << "Connection: close\r\n\r\n"
         << body1;

    {
        int fd = connect_tcp_local(server.port);
        send_all(fd, req1.str());
        HttpResponse r1 = parse_response(recv_all(fd));
        ::close(fd);

        INFO("Upload #1 response: " << r1.status << " " << r1.reason);
        REQUIRE(is_one_of(r1.status, {200, 201, 204, 303}));

        // Strong, crash-proof content check
        const std::string path1 = join_path(upload_dir, fname);
        std::string saved1;
        REQUIRE(read_file_to_string(path1, saved1));   // if this fails we get a clean assertion, not a segfault
        REQUIRE(saved1 == payload1);
        // Or, if you prefer the compact form:
        // REQUIRE(file_has_payload(upload_dir, fname, payload1));
    }

    // -------- Second upload (same name) --------
    const std::string boundary2 = "----CatchBoundarySecondABC";
    const std::string payload2  = "payload-two\n";
    const std::string body2     = make_multipart_body(boundary2, field, fname, payload2, ctype);

    std::ostringstream req2;
    req2 << "POST " << UPLOAD_URL() << " HTTP/1.1\r\n"
         << "Host: 127.0.0.1\r\n"
         << "User-Agent: catch2-upload-test\r\n"
         << "Accept: */*\r\n"
         << "Content-Type: multipart/form-data; boundary=" << boundary2 << "\r\n"
         << "Content-Length: " << body2.size() << "\r\n"
         << "Connection: close\r\n\r\n"
         << body2;

    int fd2 = connect_tcp_local(server.port);
    send_all(fd2, req2.str());
    HttpResponse r2 = parse_response(recv_all(fd2));
    ::close(fd2);

    INFO("Upload #2 response: " << r2.status << " " << r2.reason);

    const std::string path = join_path(upload_dir, fname);
    std::string saved;
    REQUIRE(read_file_to_string(path, saved));

    if (is_one_of(r2.status, {200, 201, 204, 303})) {
        // Overwrite/rename accepted: content should now match payload2
        REQUIRE(saved == payload2);
        // Or: REQUIRE(file_has_payload(upload_dir, fname, payload2));
    } else if (is_one_of(r2.status, {409, 412})) {
        // Overwrite rejected: original content must remain
        REQUIRE(saved == payload1);
    } else {
        FAIL("Unexpected status " << r2.status);
    }
}