#include "SessionManager.h"
#include <ctime>
#include <sstream>

/**
 * Utility for initialising Sessions
 */
void	sessionInit(Session *s, const std::string &id)
{
	s->id = id;
	s->last_activity = std::time(NULL);
	s->loggedIn = true;
}

std::string SessionManager::generateId(void)
{
	std::stringstream target;
	time_t timestamp = time(NULL);
	target << "/T-" << timestamp << "/U-" << totalSessCount;
	return (target.str());
}

SessionManager::SessionManager(void): sessions(), sessCount(0), totalSessCount(0)
{
	sessions.clear();
}

SessionManager::~SessionManager(void)
{
	for (std::map<std::string, Session *>::iterator it = sessions.begin();
		it != sessions.end(); it++)
	{
		if (it->second != NULL)
			delete it->second;
		it->second = NULL;
	}
	sessions.clear();
}

Session *SessionManager::getSession(const std::string &id)
{
	std::map<std::string, Session *>::iterator found;
	found = sessions.find(id);
	if (found != sessions.end())
	{
		found->second->last_activity = std::time(NULL);
		return (found->second);
	}
	return (NULL);
}

Session *SessionManager::newSession(void)
{
	std::string id = this->generateId();
	Session *newest = new Session;
	if (newest == NULL)
		return (NULL);
	sessionInit(newest, id);
	sessions[id] = newest;
	sessCount++;
	totalSessCount++;
	return (newest);
}
