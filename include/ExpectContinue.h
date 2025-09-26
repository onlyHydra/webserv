#if !defined(EXPECTCONTINUE_H)
#define EXPECTCONTINUE_H

#include <vector>
#include "Util.h"
// #include "FlatHeaders.h"
class Headers;
class ChainBuf;

class ExpectContinue{

	public:
		static bool needed(const Headers& h);
		static void write100(ChainBuf& out);
};

#endif // EXPECTCONTINUE_H
