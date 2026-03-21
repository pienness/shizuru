add_library(shizuru_vad STATIC
  energy_vad_device.cpp
  vad_event_device.cpp
)

target_include_directories(shizuru_vad
  PUBLIC ${CMAKE_SOURCE_DIR}
)

target_link_libraries(shizuru_vad
  PUBLIC shizuru_utils
)
