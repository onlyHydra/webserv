#ifndef EVENT_LOOP_H
#define EVENT_LOOP_H

#include <vector>

class EventLoop
{
public:
	EventLoop() : added_(), removed_() {}
	void addFD(int fd, int events) { added_.push_back(fd); }
	void removeFD(int fd) { removed_.push_back(fd); }

	const std::vector<int> &added() const { return added_; }
	const std::vector<int> &removed() const { return removed_; }

private:
	std::vector<int> added_;
	std::vector<int> removed_;
};

#endif
