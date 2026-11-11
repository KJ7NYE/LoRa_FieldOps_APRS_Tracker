#ifndef LOGGER_LEVEL_H_
#define LOGGER_LEVEL_H_

namespace logging {

enum LoggerLevel {
    // Suppresses every message (log() drops any level > _level). Used while
    // the USB port is a KISS TNC, where any text would corrupt the frames.
    LOGGER_LEVEL_OFF   = 0,
    LOGGER_LEVEL_ERROR = 1,
    LOGGER_LEVEL_WARN,
    LOGGER_LEVEL_INFO,
    LOGGER_LEVEL_DEBUG
};

} // namespace logging

#endif
