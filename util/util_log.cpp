#include <iostream>

#include "util_log.h"
#include "util_debug.h"

namespace dxbc_spv::util {

thread_local Logger* Logger::s_logger = nullptr;


Logger::Logger() {
  dxbc_spv_assert(!s_logger);

  /* Safe because thread-local; there is no way the logger gets
   * invoked before the object is sufficiently initialized. */
  s_logger = this;
}


Logger::~Logger() {
  dxbc_spv_assert(s_logger == this);
  s_logger = nullptr;
}


LogLevel Logger::minSeverity() {
  if (s_logger)
    return s_logger->getMinimumSeverity();

  return LogLevel::eInfo;
}


void Logger::log(LogLevel severity, const char* message) {
  if (s_logger) {
    s_logger->message(severity, message);
    return;
  }

  std::cerr << severity << message << std::endl;
}


std::ostream& operator << (std::ostream& os, LogLevel severity) {
  switch (severity) {
    case LogLevel::eDebug:  return os << "debug: ";
    case LogLevel::eInfo:   return os << "info: ";
    case LogLevel::eWarn:   return os << "warn: ";
    case LogLevel::eError:  return os << "err: ";
  }

  return os << "LogLevel(" << uint32_t(severity) << ")";

}

}
