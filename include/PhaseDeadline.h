// PhaseDeadline.h (C++98)
#ifndef PHASEDEADLINE_H
#define PHASEDEADLINE_H

#include "Timeout.h"

class PhaseDeadline
{
	public:
		PhaseDeadline() : active_(false), deadline_ms_(0ULL) {}

		// If timeout_ms <= 0 -> disable.
		// Otherwise set deadline to now_ms + timeout_ms (saturate on overflow).
		void reset(unsigned long long now_ms, int timeout_ms)
		{
			if (timeout_ms <= 0) {
				active_ = false;
				deadline_ms_ = 0ULL;
				return;
			}
			const unsigned long long add = (unsigned long long)timeout_ms;
			const unsigned long long maxv = ~0ULL; // ULLONG_MAX without <climits>

			active_ = true;
			if (maxv - now_ms < add)   // would overflow
				deadline_ms_ = maxv;   // saturate
			else
				deadline_ms_ = now_ms + add;
		}

		// Explicitly disable any deadline
		void cancel() { active_ = false; deadline_ms_ = 0ULL; }

		// True iff a deadline is active and now_ms reached/passed it
		bool expired(unsigned long long now_ms) const
		{
			return active_ && (now_ms >= deadline_ms_);
		}

		// Milliseconds left until deadline. 0 if inactive or expired.
		unsigned long long remaining(unsigned long long now_ms) const
		{
			if (!active_ || now_ms >= deadline_ms_) return 0ULL;
			return deadline_ms_ - now_ms;
		}

		bool active() const { return active_; }
		unsigned long long when() const { return deadline_ms_; }

		// NEW: explicit disarm
		void clear() { deadline_ms_ = 0ULL; }

	private:
		bool active_;
		unsigned long long deadline_ms_;
};

#endif // PHASEDEADLINE_H
