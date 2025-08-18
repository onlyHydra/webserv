/* --- Logger.h --- */

/* ------------------------------------------
Author: undefined
Date: 8/10/2025
------------------------------------------ */

#ifndef LOGGER_H
#define LOGGER_H

#include <iostream>
#include <string>

#define LOG_INFO(msg) (std::cout << "[INFO] " << msg << std::endl)
#define LOG_ERROR(msg) (std::cerr << "[ERROR] " << msg << std::endl)
#define LOG_DEBUG(msg) (std::cout << "[DEBUG] " << msg << std::endl)


class Logger {
public:
    Logger();
    ~Logger();

private:

};

#endif // LOGGER_H
