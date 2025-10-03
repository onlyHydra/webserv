/* --- CgiProcess.h --- */

/* ------------------------------------------
Author: undefined
Date: 8/10/2025
------------------------------------------ */

#ifndef CGIPROCESS_H
#define CGIPROCESS_H
#include "VirtualServer.h"
#include "TimeUtil.h"
#include <string>
#include <vector>
#include <sys/types.h> // pid_t

struct CgiSpec;  


class CgiProcess {
public:
	CgiProcess();
	~CgiProcess();

	// Convenience overload
	bool spawn(const CgiSpec& spec,
			const std::string& scriptPath,
			const std::vector<std::string>& envv,
			std::vector<int> tracked);

	// FDs for parent side: write body to inFD(), read response from outFD()
	int  inFD()  const { return _in;  }  // parent writes → child's stdin
	int  outFD() const { return _out; }  // parent reads  ← child's stdout/stderr
	pid_t pid()  const { return _pid; }

	// Close our pipe ends (idempotent)
	void closeIn();
	void closeOut();
	void closeBoth();

	// Nonblocking status check; returns:
	//  0 = still running, >0 = exited (status code), <0 = error
	int  waitNonBlocking(int* raw_status);

	// Send SIGKILL (if still running) and reap
	void terminate();

	// Helpers
	static bool setNonBlocking(int fd);
	static bool setCloseOnExec(int fd);

	// Deadline helpers (you can compare nowMs() with deadlineMs())
	unsigned long long        deadlineMs() const { return _deadline; }



private:
	pid_t              _pid;
	int                _in;       // write end to child's stdin
	int                _out;      // read end from child's stdout/stderr
	unsigned long long _deadline; // absolute ms (for external timeout logic)
};

#endif // CGIPROCESS_H

