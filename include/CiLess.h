/* --- CiLess.h --- */

/* ------------------------------------------
Author: undefined
Date: 8/10/2025
------------------------------------------ */

#ifndef CILESS_H
#define CILESS_H

# include <string>
# include <ctype.h>
# include "Util.h"

using namespace std;

/**
 * Class for overwriting map containers Compare Function,
 * in order to make comparing case insensitive, 
 * while storing the result as case sensitive
 */
class CiLess {
public:
	/**
	 * Constructor
	 */
	CiLess();
	/**
	 * Destructor
	 */
	~CiLess();
	/**
	 * Operator override, used for map key comparing
	 * ---------------------------------------------
	 * This test is run twice, once with left as the new key,
	 * and once with right being the new key,
	 * while the other parameter is the currently checked, existing key
	 * test(a, b)	test(b, a)	Meaning
	 * false		false		a == b (equal)
	 * true			false		a < b (a before b)
	 * false		true		b < a (b before a)
	 * true			true		N/A
	 * The result of both tests is used for sorting / overwriting
	 * @returns true, if left is "less then" right
	 * @returns false otherwise
	 */
	bool operator()(const string &left, const string &right) const;
};

#endif // CILESS_H
