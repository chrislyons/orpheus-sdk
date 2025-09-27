add_executable(orpheus_tests
  abi_smoke.cpp
  session_roundtrip.cpp
)

target_link_libraries(orpheus_tests
  PRIVATE
    Orpheus::core
    GTest::gtest_main
)

if(ORP_EXTRA_UBSAN_LIB)
  target_link_libraries(orpheus_tests PRIVATE ${ORP_EXTRA_UBSAN_LIB})
endif()

target_include_directories(orpheus_tests
  PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

include(${CMAKE_SOURCE_DIR}/cmake/CompilerWarnings.cmake)
orpheus_enable_warnings(orpheus_tests)

include(GoogleTest)
gtest_discover_tests(orpheus_tests)

set(FIND_PACKAGE_SOURCE_DIR ${CMAKE_SOURCE_DIR}/tests/cmake/find_package)
set(FIND_PACKAGE_BINARY_DIR ${CMAKE_BINARY_DIR}/tests/find_package_build)

add_test(NAME cmake_find_package
  COMMAND ${CMAKE_COMMAND}
    -DOrpheusSDK_DIR=${CMAKE_BINARY_DIR}
    -Dsource_dir=${FIND_PACKAGE_SOURCE_DIR}
    -Dbinary_dir=${FIND_PACKAGE_BINARY_DIR}
    -P ${CMAKE_SOURCE_DIR}/tests/cmake/run_find_package.cmake)

add_executable(orpheus_abi_link
  abi_link/main.cpp
)

target_compile_definitions(orpheus_abi_link
  PRIVATE
    ORPHEUS_ABI_LINK_DIR="$<TARGET_FILE_DIR:orpheus_session>"
    ORPHEUS_SESSION_LIB="$<TARGET_FILE_NAME:orpheus_session>"
    ORPHEUS_CLIPGRID_LIB="$<TARGET_FILE_NAME:orpheus_clipgrid>"
    ORPHEUS_RENDER_LIB="$<TARGET_FILE_NAME:orpheus_render>"
)

target_link_libraries(orpheus_abi_link
  PRIVATE
    ${CMAKE_DL_LIBS}
)

target_include_directories(orpheus_abi_link
  PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

orpheus_enable_warnings(orpheus_abi_link)

add_test(NAME abi_link COMMAND orpheus_abi_link)
