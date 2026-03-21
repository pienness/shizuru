add_library(shizuru_io_probe STATIC
  log_device.cpp
  pcm_dump_device.cpp
)

target_include_directories(shizuru_io_probe
  PUBLIC ${CMAKE_SOURCE_DIR}
)

target_link_libraries(shizuru_io_probe
  PUBLIC shizuru_utils
)
