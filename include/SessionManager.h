/* --- SessionManager.h --- */

/* ------------------------------------------
Author: lilly81324
Date: 17/09/2025
------------------------------------------ */

#ifndef SESSIONMANAGER_H
# define SESSIONMANAGER_H

#include <string>
#include <map>

#include <iostream>

struct Session
{
	std::string id;
	time_t last_activity;
	bool loggedIn;
};

class SessionManager
{
	private:
		std::map<std::string, Session *> sessions;
		/**
		 * @brief Generates unique Session ID
		 */
		std::string generateId(void);
	public:
		unsigned int sessCount;
		unsigned int totalSessCount;

		/**
		 * @brief Creates new Session Manager
		 */
		SessionManager(void);

		/**
		 * @brief Deletes and frees all registered Sessions
		 */
		~SessionManager(void);

		/**
		 * @brief Tries to get Session based off Cookie
		 * @returns Session, or NULL if no Session for that Cookie registered
		 */
		Session *getSession(const std::string &id);
		/**
		 * @brief Generates a new Session and returns it
		 * @note find the id in the session.id field
		 */
		Session *newSession(void);
};

#endif