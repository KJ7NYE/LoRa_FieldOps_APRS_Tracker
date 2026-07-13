#ifndef _LOGGER_H_
#define _LOGGER_H_

#include <Arduino.h>
#include <stdarg.h>

#include "logger_level.h"

namespace logging {

class Logger {
public:
    Logger() : _serial(&Serial), _level(LOGGER_LEVEL_INFO) {}
    Logger(LoggerLevel level) : _serial(&Serial), _level(level) {}
    Logger(Stream* serial) : _serial(serial), _level(LOGGER_LEVEL_INFO) {}
    Logger(Stream* serial, LoggerLevel level) : _serial(serial), _level(level) {}
    ~Logger() = default;

    void setSerial(Stream* serial) { _serial = serial; }
    void setDebugLevel(LoggerLevel level) { _level = level; }

    void log(LoggerLevel level, const String& module, const char* fmt, ...) {
        if (level > _level || _serial == nullptr) return;
        char buf[256];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        _serial->print('[');
        _serial->print(levelTag(level));
        _serial->print("][");
        _serial->print(module);
        _serial->print("] ");
        _serial->println(buf);
    }

private:
    static const char* levelTag(LoggerLevel l) {
        switch (l) {
            case LOGGER_LEVEL_ERROR: return "ERROR";
            case LOGGER_LEVEL_WARN:  return "WARN";
            case LOGGER_LEVEL_INFO:  return "INFO";
            case LOGGER_LEVEL_DEBUG: return "DEBUG";
            default:                 return "";
        }
    }

    Stream*     _serial;
    LoggerLevel _level;
};

} // namespace logging

#endif
