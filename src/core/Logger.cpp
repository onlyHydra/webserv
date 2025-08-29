/* --- Logger.cpp --- */

/* ------------------------------------------
author: undefined
date: 8/10/2025
------------------------------------------ */

#include "Logger.h"

#ifdef __unix__
# include <unistd.h>
#endif

// ---- static state ----
bool        Logger::s_enabled   = true;
Logger::Level Logger::s_level   = Logger::INFO_;
bool        Logger::s_useColors = false;

// ---- config ----
void Logger::setEnabled(bool on)   { s_enabled = on; }
bool Logger::isEnabled()           { return s_enabled; }

void Logger::setLevel(Level lvl)   { s_level = lvl; }
Logger::Level Logger::getLevel()   { return s_level; }

void Logger::setUseColors(bool on) { s_useColors = on; }
bool Logger::getUseColors()        { return s_useColors; }

// ---- helpers ----
bool Logger::wouldLog(Level lvl) { return lvl >= s_level; }

std::ostream& Logger::streamFor(Level lvl) {
    // INFO/DEBUG -> stdout ; WARN/ERROR -> stderr
    return (lvl >= WARN_) ? std::cerr : std::cout;
}

const char* Logger::levelName(Level lvl) {
    switch (lvl) {
    case DEBUG_: return "DEBUG";
    case INFO_:  return "INFO";
    case WARN_:  return "WARN";
    case ERROR_: return "ERROR";
    }
    return "?";
}

std::string Logger::timestamp() {
    // Simple YYYY-mm-dd HH:MM:SS
    char buf[32];
    std::time_t t = std::time(0);
#if defined(_MSC_VER)
    std::tm tmv;
    localtime_s(&tmv, &t);
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmv);
#else
    std::tm* tmv = std::localtime(&t);
    if (!tmv) return "";
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tmv);
#endif
    return std::string(buf);
}

const char* Logger::colorStart(Level lvl) {
    if (!s_useColors) return "";
    switch (lvl) {
    case DEBUG_: return "\033[36m"; // cyan
    case INFO_:  return "\033[32m"; // green
    case WARN_:  return "\033[33m"; // yellow
    case ERROR_: return "\033[31m"; // red
    }
    return "";
}

const char* Logger::colorEnd() {
    return s_useColors ? "\033[0m" : "";
}

// ---- core logging ----
void Logger::log(Level lvl, const std::string& msg) {
    if (!s_enabled || !wouldLog(lvl)) return;

    std::ostream& os = streamFor(lvl);
    os << "[" << timestamp() << "] "
       << colorStart(lvl) << levelName(lvl) << colorEnd()
       << " " << msg << std::endl;
}
