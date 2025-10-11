# SPDX-License-Identifier: MIT
include(FetchContent)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/googletest/CMakeLists.txt")
  # Use vendored googletest when present
  add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/googletest EXCLUDE_FROM_ALL)
else()
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(googletest)
endif()
