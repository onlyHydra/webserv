/* --- Logger.h --- */

/* ------------------------------------------
Author: undefined
Date: 8/10/2025
------------------------------------------ */

#ifndef LOGGER_H
#define LOGGER_H

#include <iostream>
#include <sstream>
#include <string>
#include <ctime>

class Logger {
public:
    // Log levels (ordered: lower = more verbose)
    enum Level {
        DEBUG_ = 0,
        INFO_  = 1,
        WARN_  = 2,
        ERROR_ = 3
    };

    // Configuration
    static void setEnabled(bool on);
    static bool isEnabled();

    static void setLevel(Level lvl);
    static Level getLevel();

    static void setUseColors(bool on);
    static bool getUseColors();

    // Convenience
    static bool wouldLog(Level lvl);                 // true if lvl >= current level
    static void log(Level lvl, const std::string&);  // core logging call

private:
    // Helpers
    static std::ostream& streamFor(Level lvl);
    static const char*   levelName(Level lvl);
    static std::string   timestamp();
    static const char*   colorStart(Level lvl);
    static const char*   colorEnd();

    // State
    static bool  s_enabled;
    static Level s_level;
    static bool  s_useColors;
};

// ---------- Streaming-style convenience macros ----------
// Usage: LOG_INFO("effective_root=" << base << " rel_path=" << rel);
#define LOG_DEBUG(msg) do { \
    if (Logger::isEnabled() && Logger::wouldLog(Logger::DEBUG_)) { \
        std::ostringstream _oss_; _oss_ << msg; \
        Logger::log(Logger::DEBUG_, _oss_.str()); \
    } \
} while(0)

#define LOG_INFO(msg) do { \
    if (Logger::isEnabled() && Logger::wouldLog(Logger::INFO_)) { \
        std::ostringstream _oss_; _oss_ << msg; \
        Logger::log(Logger::INFO_, _oss_.str()); \
    } \
} while(0)

#define LOG_WARN(msg) do { \
    if (Logger::isEnabled() && Logger::wouldLog(Logger::WARN_)) { \
        std::ostringstream _oss_; _oss_ << msg; \
        Logger::log(Logger::WARN_, _oss_.str()); \
    } \
} while(0)

#define LOG_ERROR(msg) do { \
    if (Logger::isEnabled() && Logger::wouldLog(Logger::ERROR_)) { \
        std::ostringstream _oss_; _oss_ << msg; \
        Logger::log(Logger::ERROR_, _oss_.str()); \
    } \
} while(0)

#endif // LOGGER_H

