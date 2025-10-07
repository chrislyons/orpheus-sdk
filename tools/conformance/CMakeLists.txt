# SPDX-License-Identifier: MIT
find_package(Python3 COMPONENTS Interpreter)

if(Python3_Interpreter_FOUND)
  set(ORPHEUS_CONFORMANCE_PYTHON_EXECUTABLE "${Python3_EXECUTABLE}")
else()
  set(ORPHEUS_CONFORMANCE_PYTHON_EXECUTABLE "python3")
endif()

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
    ORPHEUS_CONFORMANCE_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tools/conformance/fixtures"
    ORPHEUS_CONFORMANCE_OUTPUT_DIR="${CMAKE_BINARY_DIR}/conformance_failures"
    ORPHEUS_COMPARE_JSON_SCRIPT="${CMAKE_SOURCE_DIR}/tools/conformance/compare_json.py"
    ORPHEUS_CONFORMANCE_PYTHON="${ORPHEUS_CONFORMANCE_PYTHON_EXECUTABLE}"
)

include(GoogleTest)
set(TEST_NAME conformance_json)
add_test(NAME ${TEST_NAME} COMMAND orpheus_conformance_json)

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
