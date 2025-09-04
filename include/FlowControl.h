/* Simple per-connection flow control / backpressure helper. C++98 POD.
 */
#ifndef FLOWCONTROL_H
#define FLOWCONTROL_H

#include <cstddef>

struct FlowControl
{
    bool readPaused;
    bool writeLingerArmed;
    size_t high_water;
    size_t low_water;

    FlowControl(size_t high = 256u * 1024u, size_t low = 64u * 1024u)
        : readPaused(false), writeLingerArmed(false), high_water(high), low_water(low) {}

    bool shouldPauseRead(size_t outSize) const { 
        return outSize >= high_water; 
    }
    bool shouldResumeRead(size_t remaining) const { 
        return remaining <= low_water; 
    }

    void setReadPaused(bool v) { 
        readPaused = v; 
    }
    bool isReadPaused() const { 
        return readPaused; 
    }

    void setWriteLinger(bool v) { 
        writeLingerArmed = v; 
    }
    bool getWriteLinger() const { 
        return writeLingerArmed; 
    }
};

#endif // FLOWCONTROL_H
