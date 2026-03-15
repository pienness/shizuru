# Shared utilities (logging, etc.)
add_library(shizuru_utils STATIC
  async_logger.cpp
)

target_include_directories(shizuru_utils
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(shizuru_utils
  PUBLIC spdlog::spdlog
)
