/* --- CgiProcess.cpp --- */

/* ------------------------------------------
author: undefined
date: 8/10/2025
------------------------------------------ */

#include "CgiProcess.h"
#include "VirtualServer.h" // full definition of CgiSpec for the 3-arg overload
#include <unistd.h> // pipe, fork, dup2, execve, close
#include <fcntl.h>	// fcntl
#include <cstring>	// strerror
#include <cerrno>
#include <stdexcept>
#include <sys/wait.h> // waitpid
#include <signal.h>	  // kill

#include <vector>
#include <ctime> 

// ---- tiny helpers ---------------------------------------------------------


/* 

static inline int xclose(int &fd)

Utility that closes a file descriptor if it’s open (≥0) and resets it to -1. 
Prevents descriptor leaks and double-closes. Centralizing this small helper simplifies 
cleanup code across the class, making closeIn, closeOut, and closeBoth safe to call repeatedly 
even when descriptors were already closed. 
It’s widely used in RAII-style cleanup to reduce error-prone manual close() calls.


*/

static inline int xclose(int &fd)
{
	if (fd >= 0)
	{
		::close(fd);
		fd = -1;
	}
	return 0;
}

/**
 * @brief Closes the vector of specified fds
 * 
 * This is used, so the child can close the file descriptors
 * of the sockets from Connections that are tracked in the 
 * parents Eventloop.
 * Without this, the child will be leaking fds, one for each ClientConnection
 */

void closeTrackedFds(std::vector<int> tracked)
{
	std::vector<int>::const_iterator end = tracked.end();
	for (std::vector<int>::const_iterator it = tracked.begin();
		it != end; it++)
	{
		close(*it);
	}
}



/* 

bool CgiProcess::setNonBlocking(int fd)

Sets O_NONBLOCK on a file descriptor. This is required so that CGI stdin/out 
pipes work seamlessly within the server’s non-blocking event loop. Without it, 
reading/writing could stall the entire process. Wrapping this logic ensures a consistent 
error-checked way to apply the setting across all descriptors used in CGI.

*/

bool CgiProcess::setNonBlocking(int fd)
{
	int fl = ::fcntl(fd, F_GETFL, 0);
	if (fl < 0)
		return false;
	return ::fcntl(fd, F_SETFL, fl | O_NONBLOCK) == 0;
}


/* 

bool CgiProcess::setCloseOnExec(int fd)

Sets the FD_CLOEXEC flag, ensuring file descriptors do not leak into child processes across 
future execve calls. This is vital when the server later spawns additional CGI programs; 
stray descriptors could cause resource leaks or unintended sharing. 
Encapsulation ensures consistent, deliberate application of this safeguard.

*/

bool CgiProcess::setCloseOnExec(int fd)
{
	int fl = ::fcntl(fd, F_GETFD, 0);
	if (fl < 0)
		return false;
	return ::fcntl(fd, F_SETFD, fl | FD_CLOEXEC) == 0;
}

// ---- lifecycle ------------------------------------------------------------


/* 

CgiProcess::CgiProcess() / ~CgiProcess()

The constructor initializes process state: _pid=-1, _in=-1, _out=-1, _deadline=0. 
Destructor calls terminate() to reap/kick children and close descriptors. 
This RAII setup ensures processes cannot outlive their parent’s object lifetime, 
avoiding zombie processes and leaks. The lightweight constructor/destructor 
design is essential for safe stack or container-based storage of CgiProcess objects.


*/

CgiProcess::CgiProcess()
	: _pid(-1), _in(-1), _out(-1), _deadline(0ULL) {}

CgiProcess::~CgiProcess()
{
	terminate(); // safe if already reaped/closed
}


/* 

bool CgiProcess::spawn(const CgiSpec&, const std::string&, const std::vector<std::string>&)

High-level spawn function. Creates two pipes (stdin/stdout), 
forks, and in the child: duplicates pipe ends, closes extras, builds argv/envp, and calls execve. 
Parent keeps the write end of stdin and read end of stdout, 
applies non-blocking + CLOEXEC, and records child PID. 
This variant simplifies launching CGI scripts given only a spec, 
script path, and environment variables. It isolates complexity, 
enabling handlers to run CGIs without worrying about low-level pipe setup.

*/

// High-level convenience overload: build argv/envp and delegate
bool CgiProcess::spawn(const CgiSpec &spec,
					const std::string &script_path,
					const std::vector<std::string> &envv,
					std::vector<int> tracked)
{
	closeBoth(); // if previously used
	_pid = -1;
	_in  = -1;  // parent will WRITE to child's stdin
	_out = -1;  // parent will READ  from child's stdout

	int pin[2]  = { -1, -1 }; // pipe for child's stdin  (parent writes -> child reads)
	int pout[2] = { -1, -1 }; // pipe for child's stdout (child writes -> parent reads)

	if (::pipe(pin) < 0)
		return false;
	if (::pipe(pout) < 0)
	{
		::close(pin[0]);
		::close(pin[1]);
		return false;
	}

	pid_t pid = ::fork();
	if (pid < 0)
	{
		::close(pin[0]);
		::close(pin[1]);
		::close(pout[0]);
		::close(pout[1]);
		return false;
	}

	if (pid == 0)
	{
		// ---- Child ----
		// stdin: read end of pin
		::dup2(pin[0], STDIN_FILENO);
		// stdout: write end of pout
		::dup2(pout[1], STDOUT_FILENO);

		// close all pipe fds in child
		::close(pin[0]);
		::close(pin[1]);
		::close(pout[0]);
		::close(pout[1]);
		
		closeTrackedFds(tracked);

		// Build argv and envp (you already have helpers; keep minimal here)
		std::vector<char*> argv;
		argv.push_back(const_cast<char*>(spec.bin.c_str()));     // /usr/bin/python3 (or interpreter)
		argv.push_back(const_cast<char*>(script_path.c_str()));  // /path/to/script.py
		argv.push_back(0);

		std::vector<char*> envp;
		envp.reserve(envv.size() + 1);
		for (size_t i = 0; i < envv.size(); ++i)
			envp.push_back(const_cast<char*>(envv[i].c_str()));
		envp.push_back(0);

		::execve(spec.bin.c_str(), &argv[0], &envp[0]);

		// ---- execve failed: stay within allowed calls ----
		// Best effort message (ignore errors)
		const char msg[] = "execve failed\n";
		(void)::write(STDERR_FILENO, msg, sizeof(msg) - 1);

		// Close stdio so we don't hold any pipe ends
		(void)::close(STDIN_FILENO);
		(void)::close(STDOUT_FILENO);
		(void)::close(STDERR_FILENO);

		// Block forever; parent will enforce timeouts and reap us.
		// select() is allowed; this avoids CPU spin.
		for (;;)
			(void)::select(0, 0, 0, 0, 0);
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
	fl = ::fcntl(_in, F_GETFL, 0);
	if (fl >= 0) ::fcntl(_in, F_SETFL, fl | O_NONBLOCK);
	fl = ::fcntl(_out, F_GETFL, 0);
	if (fl >= 0) ::fcntl(_out, F_SETFL, fl | O_NONBLOCK);
	fl = ::fcntl(_in, F_GETFD, 0);
	if (fl >= 0) ::fcntl(_in, F_SETFD, fl | FD_CLOEXEC);
	fl = ::fcntl(_out, F_GETFD, 0);
	if (fl >= 0) ::fcntl(_out, F_SETFD, fl | FD_CLOEXEC);

	return true;
}

/* 

void CgiProcess::closeIn() / closeOut() / closeBoth()

Close parent ends of CGI pipes using xclose. These are used when the server no longer needs to 
write to CGI stdin (after sending request body) or read from stdout (after response completed). 
Encapsulation provides safe repeated calls and simplifies cleanup. 
It prevents dangling FDs from lingering in the event loop.

*/

void CgiProcess::closeIn()
{
	(void)xclose(_in);
}
void CgiProcess::closeOut()
{
	(void)xclose(_out);
}
void CgiProcess::closeBoth()
{
	closeIn();
	closeOut();
}


/* 


int CgiProcess::waitNonBlocking(int *raw_status)

Checks CGI child process status with waitpid(..., WNOHANG). 
Returns 0 if still running, -1 on error, or exit/signal code when finished. 
Also closes descriptors and resets _pid after reaping. This allows the server to poll whether the CGI has finished without blocking. 
Returning conventional exit codes simplifies debugging and HTTP response logic 
(like surfacing 500 errors on crashes).

*/

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


// helper lambda-ish for waiting with timeout
void wait(unsigned long long ms)
{
	unsigned long long start = TimeUtil::nowMs();
	while (TimeUtil::nowMs() < start + ms)
	{
		// Timeout for 10 ms by doing nothing
		struct timeval tv = {0, 0};
		tv.tv_sec = 0;
		tv.tv_usec = 10 * 1000;
		select(0, NULL, NULL, NULL, &tv);
	}
}


/* 


void CgiProcess::terminate()

Safely tears down the child. First tries waitNonBlocking; 
if still running, sends SIGKILL, then reaps with waitpid. 
Finally closes descriptors and resets _pid. Ensures no zombie children remain and no dangling pipe endpoints survive. 
This is the failsafe used on timeouts, errors, or handler destruction. 
It’s critical for resource hygiene and stability in a multi-client environment.

*/

void CgiProcess::terminate()
{
	if (_pid > 0)
	{
		// Try to kill child gently
		kill(_pid, SIGTERM);
		// try to reap without killing first
		int dummy = 0;
		int rc = waitNonBlocking(&dummy);
		// If process is still running after SIGTERM was sent, chill first, kill later
		if (rc == 0)
			wait(1000);
		
		// If still running, despite Signal and enough time, force kill it
		rc = waitNonBlocking(&dummy);
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
