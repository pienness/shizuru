# Core agent framework library
add_library(shizuru_core STATIC
  controller/controller.cpp
  context/context_strategy.cpp
  policy/policy_layer.cpp
  session/session.cpp
  logging/logger.cpp
)

target_include_directories(shizuru_core
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(shizuru_core
  PUBLIC spdlog::spdlog
)
