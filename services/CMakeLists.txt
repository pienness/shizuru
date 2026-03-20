include(FetchContent)

find_package(OpenSSL QUIET)

if(NOT OpenSSL_FOUND AND WIN32)
  # Try common non-standard locations (Conda, standalone installers).
  if(NOT OPENSSL_ROOT_DIR)
    foreach(_win_path
        "$ENV{CONDA_PREFIX}/Library"
        "C:/Program Files/OpenSSL-Win64"  "C:/OpenSSL-Win64"
        "C:/Program Files/OpenSSL"        "C:/OpenSSL")
      if(IS_DIRECTORY "${_win_path}")
        set(OPENSSL_ROOT_DIR "${_win_path}")
        break()
      endif()
    endforeach()
    if(OPENSSL_ROOT_DIR)
      message(STATUS "OpenSSL hint: ${OPENSSL_ROOT_DIR}")
      find_package(OpenSSL QUIET)
    endif()
  endif()

  if(NOT OpenSSL_FOUND)
    FetchContent_Declare(
      openssl_cmake
      GIT_REPOSITORY https://github.com/janbar/openssl-cmake.git
      GIT_TAG        3.3.1-20240911
    )
    FetchContent_MakeAvailable(openssl_cmake)
    if(NOT TARGET OpenSSL::SSL)
      add_library(OpenSSL::SSL    ALIAS ssl)
      add_library(OpenSSL::Crypto ALIAS crypto)
    endif()
    set(OpenSSL_FOUND TRUE)
  endif()
endif()

if(OpenSSL_FOUND)
  set(HTTPLIB_REQUIRE_OPENSSL ON  CACHE BOOL "" FORCE)
else()
  message(WARNING "OpenSSL not found — HTTPS disabled. "
                  "Use an HTTP endpoint (e.g., http://localhost:11434).")
  set(HTTPLIB_REQUIRE_OPENSSL OFF CACHE BOOL "" FORCE)
endif()

add_subdirectory(utils)
add_subdirectory(llm)
add_subdirectory(asr)
add_subdirectory(io)
add_subdirectory(tts)
