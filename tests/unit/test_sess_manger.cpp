#include <catch2/catch_all.hpp>
#include "SessionManager.h"

TEST_CASE("Testing Session Manager", "[session_manager]")
{
	SECTION("Creation")
	{
		SessionManager ses;
		std::string id;
		Session *ptr;
		REQUIRE(ses.getSession("") == NULL);
		REQUIRE(ses.getSession("This is garabge, utter nonsense!") == NULL);
		ptr = ses.newSession();
		REQUIRE(ptr != NULL);
		REQUIRE(ses.getSession(ptr->id) == ptr);
		ptr = ptr = ses.newSession();
		REQUIRE(ses.getSession(ptr->id) == ptr);
		for (int i = 0; i < 20; i++)
			REQUIRE(ses.newSession() != ses.newSession());
		REQUIRE(ses.sessCount == 42);
		REQUIRE(ses.totalSessCount == 42);
	}
}