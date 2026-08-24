// Copyright (c) 2026 mysql-wire-cpp contributors.
// SPDX-License-Identifier: MIT

/**
 * @file log.h
 * @brief Small thread-safe line logger for internal runtime diagnostics.
 */

#pragma once

#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>

namespace mysql_wire {

namespace detail {

inline auto LogMutex() -> std::mutex & {
  static std::mutex mutex;
  return mutex;
}

inline void WriteLogLine(const char *level, const std::string &message) {
  const std::lock_guard<std::mutex> lock(LogMutex());
  std::clog << "[mysql-wire] [" << level << "] " << message << '\n';
}

template <typename... Args> void Log(const char *level, Args &&...args) {
  std::ostringstream message;
  (message << ... << std::forward<Args>(args));
  WriteLogLine(level, message.str());
}

} // namespace detail

template <typename... Args> void LogInfo(Args &&...args) {
  detail::Log("INFO", std::forward<Args>(args)...);
}

template <typename... Args> void LogWarning(Args &&...args) {
  detail::Log("WARN", std::forward<Args>(args)...);
}

template <typename... Args> void LogError(Args &&...args) {
  detail::Log("ERROR", std::forward<Args>(args)...);
}

} // namespace mysql_wire
