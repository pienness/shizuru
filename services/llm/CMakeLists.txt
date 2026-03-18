include(FetchContent)

# nlohmann/json (header-only JSON library) — URL download is much faster than git clone
FetchContent_Declare(
  nlohmann_json
  URL      https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

# cpp-httplib (header-only HTTP library) — URL download
FetchContent_Declare(
  httplib
  URL      https://github.com/yhirose/cpp-httplib/archive/refs/tags/v0.18.1.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
# OpenSSL is needed for HTTPS (e.g., api.openai.com).
# On Linux, OpenSSL is typically in standard paths and find_package works directly.
# On macOS and Windows it often lives in non-standard locations, so we add
# platform-specific hints below.  Users can always override with:
#   cmake -DOPENSSL_ROOT_DIR=<path> ...
if(NOT OPENSSL_ROOT_DIR)
  if(APPLE)
    # Homebrew keg-only OpenSSL paths (Apple Silicon and Intel).
    foreach(_brew_prefix
        /opt/homebrew/opt/openssl@3  /opt/homebrew/opt/openssl
        /usr/local/opt/openssl@3     /usr/local/opt/openssl)
      if(IS_DIRECTORY "${_brew_prefix}")
        set(OPENSSL_ROOT_DIR "${_brew_prefix}")
        break()
      endif()
    endforeach()
  elseif(WIN32)
    # 1) Conda / Miniconda active environment.
    if(DEFINED ENV{CONDA_PREFIX} AND IS_DIRECTORY "$ENV{CONDA_PREFIX}/Library")
      set(OPENSSL_ROOT_DIR "$ENV{CONDA_PREFIX}/Library")
    endif()
    # 2) Common Windows installer locations.
    if(NOT OPENSSL_ROOT_DIR)
      foreach(_win_path
          "C:/Program Files/OpenSSL-Win64"  "C:/OpenSSL-Win64"
          "C:/Program Files/OpenSSL"        "C:/OpenSSL")
        if(IS_DIRECTORY "${_win_path}")
          set(OPENSSL_ROOT_DIR "${_win_path}")
          break()
        endif()
      endforeach()
    endif()
  endif()
  if(OPENSSL_ROOT_DIR)
    message(STATUS "OpenSSL hint: ${OPENSSL_ROOT_DIR}")
  endif()
endif()
find_package(OpenSSL QUIET)
if(OpenSSL_FOUND)
  set(HTTPLIB_REQUIRE_OPENSSL ON CACHE BOOL "Require OpenSSL for HTTPS" FORCE)
else()
  message(WARNING "OpenSSL not found — HTTPS support disabled. "
                  "Set base_url to an HTTP endpoint (e.g., http://localhost:11434).")
  set(HTTPLIB_REQUIRE_OPENSSL OFF CACHE BOOL "" FORCE)
endif()
FetchContent_MakeAvailable(httplib)

add_library(shizuru_llm STATIC
  json_parser.cpp
  openai_client.cpp
)

target_include_directories(shizuru_llm
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(shizuru_llm
  PUBLIC  shizuru_core
  PUBLIC  nlohmann_json::nlohmann_json
  PRIVATE httplib::httplib
)
