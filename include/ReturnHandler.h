/* --- PutPatchHandler.h --- */

/* ------------------------------------------
Author: undefined
Date: 8/16/2025
------------------------------------------ */

#ifndef RETURNHANDLER_H
# define RETURNHANDLER_H

# include "ClientConnection.h"
# include <sstream>

class ReturnHandler
{
	public:
		static std::string handle(int status, const std::string &option);
};


#endif // RETURNHANDLER_H
