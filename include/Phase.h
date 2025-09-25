#ifndef PHASE_H
#define PHASE_H

enum Phase
	{
		PH_READ_HEADERS = 0,
		PH_ROUTE_SELECT,
		PH_PRECHECK,
		PH_READ_BODY,
		PH_ROUTE,
		PH_WRITE,
		PH_CLOSE,

		// --- Legacy aliases for compatibility ---
		PH_RECV = PH_READ_HEADERS,  // if some code still uses PH_RECV
		PH_READ = PH_READ_HEADERS   // what your new code expects
};

#endif // PHASE_H

