/* --- CgiProcess.cpp --- */

/* ------------------------------------------
author: undefined
date: 8/10/2025
------------------------------------------ */

#include "CgiProcess.h"
#include "VirtualServer.h" // full definition of CgiSpec for the 3-arg overload

#include <unistd.h> // pipe, fork, dup2, execve, close
#include <fcntl.h>  // fcntl
#include <cstring>  // strerror
#include <cerrno>
#include <stdexcept>
#include <sys/wait.h> // waitpid
#include <signal.h>   // kill
#include <sys/time.h> // gettimeofday
#include <vector>

// ---- tiny helpers ---------------------------------------------------------

static inline int xclose(int &fd)
{
    if (fd >= 0)
    {
        ::close(fd);
        fd = -1;
    }
    return 0;
}

unsigned long long CgiProcess::nowMs()
{
    struct timeval tv;
    ::gettimeofday(&tv, 0);
    return (unsigned long long)tv.tv_sec * 1000ULL + (unsigned long long)(tv.tv_usec / 1000ULL);
}

bool CgiProcess::setNonBlocking(int fd)
{
    int fl = ::fcntl(fd, F_GETFL, 0);
    if (fl < 0)
        return false;
    return ::fcntl(fd, F_SETFL, fl | O_NONBLOCK) == 0;
}

bool CgiProcess::setCloseOnExec(int fd)
{
    int fl = ::fcntl(fd, F_GETFD, 0);
    if (fl < 0)
        return false;
    return ::fcntl(fd, F_SETFD, fl | FD_CLOEXEC) == 0;
}

// ---- lifecycle ------------------------------------------------------------

CgiProcess::CgiProcess()
    : _pid(-1), _in(-1), _out(-1), _deadline(0ULL) {}

CgiProcess::~CgiProcess()
{
    terminate(); // safe if already reaped/closed
}

// High-level convenience overload: build argv/envp and delegate
bool CgiProcess::spawn(const CgiSpec &spec,
                       const std::string &script_path,
                       const std::vector<std::string> &envv)
{
    closeBoth(); // if previously used
    _pid = -1;
    _in  = -1;   // parent will WRITE to child's stdin
    _out = -1;   // parent will READ  from child's stdout

    int pin[2]  = { -1, -1 }; // pipe for child's stdin  (parent writes -> child reads)
    int pout[2] = { -1, -1 }; // pipe for child's stdout (child writes -> parent reads)

    if (::pipe(pin)  < 0) return false;
    if (::pipe(pout) < 0) { ::close(pin[0]); ::close(pin[1]); return false; }

    pid_t pid = ::fork();
    if (pid < 0) {
        ::close(pin[0]);  ::close(pin[1]);
        ::close(pout[0]); ::close(pout[1]);
        return false;
    }

    if (pid == 0) {
        // ---- Child ----
        // stdin: read end of pin
        ::dup2(pin[0],  STDIN_FILENO);
        // stdout: write end of pout
        ::dup2(pout[1], STDOUT_FILENO);

        // close all pipe fds in child
        ::close(pin[0]);  ::close(pin[1]);
        ::close(pout[0]); ::close(pout[1]);

        // Build argv and envp (you already have helpers; keep minimal here)
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(spec.bin.c_str())); // /usr/bin/python3
        argv.push_back(const_cast<char*>(script_path.c_str()));      // /path/to/script.py
        argv.push_back(0);

        std::vector<char*> envp;
        envp.reserve(envv.size() + 1);
        for (size_t i = 0; i < envv.size(); ++i)
            envp.push_back(const_cast<char*>(envv[i].c_str()));
        envp.push_back(0);

        ::execve(spec.bin.c_str(), &argv[0], &envp[0]);
        _exit(127); // exec failed
    }

    // ---- Parent ----
    _pid = pid;

    // Parent writes to child's stdin -> keep pin[1], close pin[0]
    ::close(pin[0]);
    _in = pin[1];

    // Parent reads child's stdout -> keep pout[0], close pout[1]
    ::close(pout[1]);
    _out = pout[0];

    // Set parent ends to NONBLOCK + CLOEXEC
    int fl;
    fl = ::fcntl(_in,  F_GETFL, 0); if (fl >= 0) ::fcntl(_in,  F_SETFL,  fl | O_NONBLOCK);
    fl = ::fcntl(_out, F_GETFL, 0); if (fl >= 0) ::fcntl(_out, F_SETFL, fl | O_NONBLOCK);
    fl = ::fcntl(_in,  F_GETFD, 0); if (fl >= 0) ::fcntl(_in,  F_SETFD, fl | FD_CLOEXEC);
    fl = ::fcntl(_out, F_GETFD, 0); if (fl >= 0) ::fcntl(_out, F_SETFD, fl | FD_CLOEXEC);

    return true;
}


// Low-level spawn: do the actual fork/exec (stub for now; returns false)
bool CgiProcess::spawn(const std::string &bin,
                       const std::string &script,
                       char *const *argv,
                       char *const *envp,
                       int timeout_ms)
{
	// Clean up any previous child
	terminate();

    // Establish deadline
    _deadline = (timeout_ms > 0)
                    ? (nowMs() + (unsigned long long)timeout_ms)
                    : 0ULL;

    int inPipe[2] = {-1, -1};  // parent writes -> child reads (stdin)
    int outPipe[2] = {-1, -1}; // child writes -> parent reads (stdout/stderr)

    if (::pipe(inPipe) < 0)
    {
        return false;
    }
    if (::pipe(outPipe) < 0)
    {
        ::close(inPipe[0]);
        ::close(inPipe[1]);
        return false;
    }

    pid_t pid = ::fork();
    if (pid < 0)
    {
        // fork failed
        ::close(inPipe[0]);
        ::close(inPipe[1]);
        ::close(outPipe[0]);
        ::close(outPipe[1]);
        return false;
    }

    if (pid == 0)
    {
        // ---------------- child ----------------
        // close parent ends
        ::close(inPipe[1]);
        ::close(outPipe[0]);

        // stdin from inPipe[0]
        if (::dup2(inPipe[0], STDIN_FILENO) < 0)
            _exit(126);
        // stdout to outPipe[1]
        if (::dup2(outPipe[1], STDOUT_FILENO) < 0)
            _exit(126);
        // stderr merged to stdout
        (void)::dup2(outPipe[1], STDERR_FILENO);

		// close the originals after dup
		::close(inPipe[0]);
		::close(outPipe[1]);

        // Exec the interpreter/binary with provided argv/envp.
        // argv should look like: [bin, script, NULL]
        (void)script; // only to silence unused warning; script is already in argv
        ::execve(bin.c_str(),
                 const_cast<char *const *>(argv),
                 const_cast<char *const *>(envp));
        _exit(127); // exec failed
    }

	// ---------------- parent ----------------
	_pid = pid;

	// parent keeps write-end of stdin and read-end of stdout
	::close(inPipe[0]);
	::close(outPipe[1]);

    _in = inPipe[1];
    _out = outPipe[0];

	// Make them non-blocking & close-on-exec
	(void)setNonBlocking(_in);
	(void)setNonBlocking(_out);
	(void)setCloseOnExec(_in);
	(void)setCloseOnExec(_out);

	return true;
}

void CgiProcess::closeIn() { 
    (void)xclose(_in);
}
void CgiProcess::closeOut() { 
    (void)xclose(_out); 
}
void CgiProcess::closeBoth()
{
    closeIn();
    closeOut();
}

int CgiProcess::waitNonBlocking(int *raw_status)
{
    if (_pid <= 0)
        return -1;

    int st = 0;
    pid_t r = ::waitpid(_pid, &st, WNOHANG);
    if (r == 0)
    {
        // still running
        return 0;
    }
    if (r < 0)
    {
        // wait error; treat as error
        return -1;
    }

    // child reaped
    if (raw_status)
        *raw_status = st;

    int code = 0;
    if (WIFEXITED(st))
    {
        code = WEXITSTATUS(st);
    }
    else if (WIFSIGNALED(st))
    {
        code = 128 + WTERMSIG(st); // common convention
    }

	_pid = -1;
	closeBoth();
	return code > 0 ? code : 1; // >0 means finished; return a positive number
}

void CgiProcess::terminate()
{
    if (_pid > 0)
    {
        // try to reap without killing first
        int dummy = 0;
        int rc = waitNonBlocking(&dummy);
        if (rc == 0)
        {
            // still running → kill
            ::kill(_pid, SIGKILL);
            // reap
            (void)::waitpid(_pid, &dummy, 0);
        }
        _pid = -1;
    }
    closeBoth();
}
