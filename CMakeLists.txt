cmake_minimum_required(VERSION 3.22)

project(orpheus VERSION 0.2.0 LANGUAGES CXX)

option(ORP_WITH_TESTS "Build Orpheus tests" ON)
option(ORP_BUILD_REAPER "Build the REAPER adapter" ON)
option(ORP_BUILD_MINHOST "Build the minimal host adapter" ON)
option(ENABLE_LTO "Enable interprocedural optimization" OFF)

include(CMakePackageConfigHelpers)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options("$<$<CONFIG:Debug>:-fsanitize=address>")
  add_compile_options("$<$<CONFIG:Debug>:-fsanitize=undefined>")
  add_link_options("$<$<CONFIG:Debug>:-fsanitize=address>")
  add_link_options("$<$<CONFIG:Debug>:-fsanitize=undefined>")
else()
  message(STATUS "Sanitizers are not enabled on this compiler.")
endif()

if(ENABLE_LTO)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
  if(ipo_supported)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
  else()
    message(WARNING "IPO not supported: ${ipo_error}")
  endif()
endif()

add_subdirectory(src)

if(ORP_BUILD_REAPER)
  add_subdirectory(adapters/reaper)
endif()

if(ORP_BUILD_MINHOST)
  add_subdirectory(adapters/minhost)
endif()

include(CTest)
if(ORP_WITH_TESTS)
  include(${CMAKE_SOURCE_DIR}/cmake/Dependencies.cmake)
  enable_testing()
endif()

add_subdirectory(tools)

if(ORP_WITH_TESTS)
  add_subdirectory(tests)
endif()

install(DIRECTORY include/orpheus DESTINATION include)

install(TARGETS orpheus_core
  EXPORT OrpheusSDKTargets
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin)

if(ORP_BUILD_MINHOST)
  install(TARGETS orpheus_minhost
    EXPORT OrpheusSDKTargets
    RUNTIME DESTINATION bin)
endif()

if(ORP_BUILD_REAPER)
  install(TARGETS reaper_orpheus
    EXPORT OrpheusSDKTargets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin)
endif()

if(TARGET orpheus_perf_render_click)
  install(TARGETS orpheus_perf_render_click
    EXPORT OrpheusSDKTargets
    RUNTIME DESTINATION bin)
endif()

install(FILES README.md LICENSE DESTINATION .)

set(INSTALL_CONFIGDIR lib/cmake/OrpheusSDK)

install(EXPORT OrpheusSDKTargets
  FILE OrpheusSDKTargets.cmake
  NAMESPACE Orpheus::
  DESTINATION ${INSTALL_CONFIGDIR})

configure_package_config_file(
  ${CMAKE_SOURCE_DIR}/cmake/OrpheusSDKConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/OrpheusSDKConfig.cmake
  INSTALL_DESTINATION ${INSTALL_CONFIGDIR})

write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/OrpheusSDKConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY AnyNewerVersion)

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/OrpheusSDKConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/OrpheusSDKConfigVersion.cmake
  DESTINATION ${INSTALL_CONFIGDIR})

export(EXPORT OrpheusSDKTargets
  FILE ${CMAKE_CURRENT_BINARY_DIR}/OrpheusSDKTargets.cmake
  NAMESPACE Orpheus::)

set(CPACK_PACKAGE_NAME "orpheus-sdk")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_FILE_NAME "orpheus-sdk-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_PACKAGE_CHECKSUM SHA256)
set(CPACK_GENERATOR "ZIP")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_SOURCE_DIR}/README.md")

include(CPack)
