# SPDX-License-Identifier: MIT
include(${CMAKE_SOURCE_DIR}/cmake/CompilerWarnings.cmake)

add_executable(orpheus_conformance_json
  json_roundtrip.cpp
)

target_link_libraries(orpheus_conformance_json
  PRIVATE
    Orpheus::core
    GTest::gtest_main
)

target_include_directories(orpheus_conformance_json
  PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/core/session
)

target_compile_definitions(orpheus_conformance_json
  PRIVATE
    ORPHEUS_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tools/fixtures"
)

orpheus_enable_warnings(orpheus_conformance_json)

include(GoogleTest)
set(TEST_NAME conformance_json)
add_test(NAME ${TEST_NAME} COMMAND orpheus_conformance_json)

set(CONFORMANCE_FIXTURES_DIR "${CMAKE_SOURCE_DIR}/tools/conformance/fixtures")
set(CONFORMANCE_OUTPUT_DIR "${CMAKE_BINARY_DIR}/tools/conformance/output")
set(CONFORMANCE_DIFF_DIR "${CMAKE_BINARY_DIR}/tools/conformance/diff")

add_test(
  NAME conformance_minhost_render_click
  COMMAND ${Python3_EXECUTABLE}
          ${CMAKE_SOURCE_DIR}/tools/conformance/run_conformance.py
          --build-dir ${CMAKE_BINARY_DIR}
          --fixtures ${CONFORMANCE_FIXTURES_DIR}
          --output ${CONFORMANCE_OUTPUT_DIR}
          --diff ${CONFORMANCE_DIFF_DIR}
)

if(ORP_BUILD_SHARED_CORE)
  add_custom_command(TARGET orpheus_conformance_json POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:orpheus_session> $<TARGET_FILE_DIR:orpheus_conformance_json>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:orpheus_clipgrid> $<TARGET_FILE_DIR:orpheus_conformance_json>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:orpheus_render> $<TARGET_FILE_DIR:orpheus_conformance_json>
  )
endif()
