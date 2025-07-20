#pragma once

#include <cstddef>
#include <cstdint>
#include <sstream>

namespace dxbc_spv::util {

/** Log level / severity */
enum class LogLevel : uint32_t {
  eDebug  = 0u,
  eInfo   = 1u,
  eWarn   = 2u,
  eError  = 3u,
};


/** Per-thread scoped logger. If no logger is created, any
 *  log messages will be sent to stderr. */
class Logger {

public:

  Logger();

  /** Unsets logger from current thread */
  virtual ~Logger();

  /** Logs message of the given severity */
  virtual void message(LogLevel severity, const char* message) = 0;

  /** Queries minimum log level. Used to avoid unnecessary
   *  string operations in some cases. */
  virtual LogLevel getMinimumSeverity() = 0;

  /** Queries minimum log level */
  static LogLevel minSeverity();

  /** Logs message to current logger */
  static void log(LogLevel severity, const char* message);

  /** Logs message with arbitrary arguments to format */
  template<typename... Args>
  static void log(LogLevel severity, const Args&... args) {
    if (severity < minSeverity())
      return;

    std::stringstream msg;
    ((msg << args), ...);

    log(severity, msg.str().c_str());
  }

  /** Convenience methods for each log level */
  template<typename... Args>
  static void err(const Args&... args) {
    log(LogLevel::eError, args...);
  }

  template<typename... Args>
  static void warn(const Args&... args) {
    log(LogLevel::eWarn, args...);
  }

  template<typename... Args>
  static void info(const Args&... args) {
    log(LogLevel::eInfo, args...);
  }

  template<typename... Args>
  static void debug(const Args&... args) {
    log(LogLevel::eDebug, args...);
  }

private:

  static thread_local Logger* s_logger;

};

std::ostream& operator << (std::ostream& os, LogLevel severity);

}

namespace dxbc_spv {
  using util::Logger;
  using util::LogLevel;
}
