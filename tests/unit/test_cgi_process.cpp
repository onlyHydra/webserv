// tests/unit/test_cgi_process.cpp
#include <catch2/catch_all.hpp>

#include "CgiProcess.h"
// #include "/VirtualServer.h"
#include "../include/VirtualServer.h"

#include <sys/stat.h>    // chmod
#include <unistd.h>      // write, close, unlink, read
#include <poll.h>        // poll
#include <fcntl.h>       // fcntl
#include <cstdio>        // fdopen, fclose
#include <cstdlib>       // mkstemp
#include <cerrno>
#include <cstring>
#include <string>
#include <vector>
#include <thread>
#include <chrono>

// --- tiny helpers ----------------------------------------------------------

static std::string make_script(const std::string& content) {
    // mkstemp requires template ending in XXXXXX, no suffix
    std::string tmpl = "/tmp/webserv_cgi_test_XXXXXX";
    std::vector<char> path(tmpl.begin(), tmpl.end());
    path.push_back('\0');

    int fd = ::mkstemp(&path[0]);
    REQUIRE(fd >= 0);

    FILE* f = ::fdopen(fd, "w");
    REQUIRE(f != NULL);

    // A shebang is fine even if we also exec via /bin/sh
    static const char shebang[] = "#!/bin/sh\n";
    REQUIRE(::fwrite(shebang, 1, sizeof(shebang) - 1, f) == sizeof(shebang) - 1);
    REQUIRE(::fwrite(content.data(), 1, content.size(), f) == content.size());
    ::fflush(f);
    ::fclose(f); // also closes fd

    // Make it executable (not strictly needed if you exec via /bin/sh script)
    REQUIRE(::chmod(&path[0], 0755) == 0);

    return std::string(&path[0]);
}


static void rm_script(const std::string& path) {
    ::unlink(path.c_str());
}

static void pump_cgi(CgiProcess& proc,
                     const std::string& body,
                     std::string& out,
                     int max_ms = 2000)
{
    out.clear();
    size_t off = 0;
    int in_fd  = proc.inFD();
    int out_fd = proc.outFD();

    const unsigned long long deadline =
        CgiProcess::nowMs() + (unsigned long long)max_ms;

    for (;;) {
        if (CgiProcess::nowMs() > deadline) break;

        struct pollfd pfds[2];
        int nfds = 0;
        if (in_fd  >= 0 && off < body.size()) { pfds[nfds].fd = in_fd;  pfds[nfds].events = POLLOUT; pfds[nfds].revents = 0; ++nfds; }
        if (out_fd >= 0)                      { pfds[nfds].fd = out_fd; pfds[nfds].events = POLLIN;  pfds[nfds].revents = 0; ++nfds; }

        int to = (int)((deadline > CgiProcess::nowMs())
                        ? (deadline - CgiProcess::nowMs())
                        : 0);
        if (to < 0) to = 0;
        int pr = ::poll(pfds, nfds, to);
        if (pr < 0 && errno == EINTR) continue;
        if (pr < 0) break; // treat as failure/exit
        if (pr == 0) continue;

        // writable -> push body
        if (in_fd >= 0 && off < body.size()) {
            int idx = (pfds[0].fd == in_fd) ? 0 : 1;
            if (pfds[idx].revents & POLLOUT) {
                ssize_t n = ::write(in_fd, body.data() + off, body.size() - off);
                if (n > 0) { off += (size_t)n; if (off == body.size()) proc.closeIn(); in_fd = proc.inFD(); }
                else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) { proc.closeIn(); in_fd = -1; }
            }
        }

        // readable -> collect output
        if (out_fd >= 0) {
            int idx = (pfds[0].fd == out_fd) ? 0 : (nfds == 2 ? 1 : 0);
            if (pfds[idx].revents & POLLIN) {
                char buf[8192];
                ssize_t n = ::read(out_fd, buf, sizeof(buf));
                if (n > 0) out.append(buf, buf + n);
                else if (n == 0) { proc.closeOut(); out_fd = -1; }
                else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) { proc.closeOut(); out_fd = -1; }
            }
        }

        // reap if finished
        int status = 0;
        int rc = proc.waitNonBlocking(&status);
        if (rc > 0 && proc.outFD() < 0) break;
        if (in_fd < 0 && out_fd < 0) break;
    }
}

// --- tests -----------------------------------------------------------------

TEST_CASE("CgiProcess spawns and we can read CGI headers/body", "[cgi][process]") {
    // Script prints CGI headers + short body
    const std::string content =
        "printf 'Status: 201 Created\\r\\n'\n"
        "printf 'Content-Type: text/plain\\r\\n'\n"
        "printf '\\r\\n'\n"
        "printf 'Hello CGI'\n";

    const std::string script = make_script(content);
    CgiSpec spec("/bin/sh", 2000);
    CgiProcess proc;

    std::vector<std::string> envv; // minimal env is fine
    REQUIRE(proc.spawn(spec, script, envv) == true);

    // fds should be non-blocking
    REQUIRE(proc.inFD()  >= 0);
    REQUIRE(proc.outFD() >= 0);
    {
        int fl = ::fcntl(proc.outFD(), F_GETFL, 0);
        REQUIRE(fl >= 0);
        CHECK((fl & O_NONBLOCK) != 0);
    }

    std::string out;
    pump_cgi(proc, /*body*/"", out, /*max_ms*/2000);

    // We expect a header block + body (not HTTP, but CGI headers)
    REQUIRE(out.find("Status: 201 Created") != std::string::npos);
    REQUIRE(out.find("Content-Type: text/plain") != std::string::npos);
    REQUIRE(out.find("\r\n\r\n") != std::string::npos);
    REQUIRE(out.find("Hello CGI") != std::string::npos);

    rm_script(script);
}

TEST_CASE("CgiProcess: stdin is delivered to script", "[cgi][process][stdin]") {
    // Script reads all stdin and outputs its length
    const std::string content =
        "IN=$(cat)\n"
        "LEN=$(printf '%s' \"$IN\" | wc -c)\n"
        "printf 'Content-Type: text/plain\\r\\n\\r\\n'\n"
        "printf 'len=%s' \"$LEN\"\n";

    const std::string script = make_script(content);
    CgiSpec spec("/bin/sh", 2000);
    CgiProcess proc;

    std::vector<std::string> envv;
    REQUIRE(proc.spawn(spec, script, envv) == true);

    const std::string body(1234, 'A'); // 1234 bytes
    std::string out;
    pump_cgi(proc, body, out, 2000);

    // Body must contain len=1234
    REQUIRE(out.find("len=1234") != std::string::npos);

    rm_script(script);
}

TEST_CASE("CgiProcess: environment variables are passed", "[cgi][process][env]") {
    // Script prints the value of X_NAME
    const std::string content =
        "printf 'Content-Type: text/plain\\r\\n\\r\\n'\n"
        "printf '%s' \"$X_NAME\"\n";

    const std::string script = make_script(content);
    CgiSpec spec("/bin/sh", 2000);
    CgiProcess proc;

    std::vector<std::string> envv;
    envv.push_back("X_NAME=BobCGI");
    REQUIRE(proc.spawn(spec, script, envv) == true);

    std::string out;
    pump_cgi(proc, "", out, 2000);

    REQUIRE(out.find("BobCGI") != std::string::npos);

    rm_script(script);
}

TEST_CASE("CgiProcess: sleeping script and manual terminate/reap", "[cgi][process][timeout]") {
    // Script that sleeps, then prints something (we'll kill earlier)
    const std::string content =
        "sleep 1\n"
        "printf 'Content-Type: text/plain\\r\\n\\r\\n'\n"
        "printf 'done'\n";

    const std::string script = make_script(content);
    // very short logical timeout for the *caller*; CgiProcess just stores deadline
    CgiSpec spec("/bin/sh", 100 /*ms*/);
    CgiProcess proc;

    std::vector<std::string> envv;
    REQUIRE(proc.spawn(spec, script, envv) == true);

    // Wait a bit; process should still be running (non-blocking wait returns 0)
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    int st = 0;
    int rc = proc.waitNonBlocking(&st);
    CHECK(rc == 0); // still running

    // Caller decides to enforce timeout -> terminate
    proc.terminate();

    // After terminate(), fds closed and child reaped/gone
    CHECK(proc.inFD() < 0);
    CHECK(proc.outFD() < 0);

    rm_script(script);
}
