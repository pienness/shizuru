#pragma once

#include <memory>
#include <string>

#include <spdlog/spdlog.h>

namespace shizuru::core {

// Logger configuration.
struct LoggerConfig {
  std::string log_file = "shizuru.log";   // File path for log output
  std::size_t max_file_size = 5 * 1024 * 1024;  // 5 MB per file
  std::size_t max_files = 3;              // Rotating file count
  spdlog::level::level_enum level = spdlog::level::info;
  bool enable_console = true;             // Also log to stdout
  std::size_t async_queue_size = 8192;    // Async queue slots
  std::size_t async_thread_count = 1;     // Background flush threads
};

// Initialize the global async logger. Call once at startup.
void InitLogger(const LoggerConfig& config = {});

// Shutdown and flush all loggers. Call before exit.
void ShutdownLogger();

// Get the shared logger instance.
std::shared_ptr<spdlog::logger> GetLogger();

// Convenience macros — zero overhead when compiled out.
#define LOG_TRACE(...)    SPDLOG_LOGGER_TRACE(::shizuru::core::GetLogger(), __VA_ARGS__)
#define LOG_DEBUG(...)    SPDLOG_LOGGER_DEBUG(::shizuru::core::GetLogger(), __VA_ARGS__)
#define LOG_INFO(...)     SPDLOG_LOGGER_INFO(::shizuru::core::GetLogger(), __VA_ARGS__)
#define LOG_WARN(...)     SPDLOG_LOGGER_WARN(::shizuru::core::GetLogger(), __VA_ARGS__)
#define LOG_ERROR(...)    SPDLOG_LOGGER_ERROR(::shizuru::core::GetLogger(), __VA_ARGS__)
#define LOG_CRITICAL(...) SPDLOG_LOGGER_CRITICAL(::shizuru::core::GetLogger(), __VA_ARGS__)

}  // namespace shizuru::core
