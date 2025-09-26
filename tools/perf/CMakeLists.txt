add_executable(orpheus_perf_render_click
  perf_render_click.cpp
)

target_link_libraries(orpheus_perf_render_click
  PRIVATE
    Orpheus::orpheus_core
)

target_include_directories(orpheus_perf_render_click
  PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

include(${CMAKE_SOURCE_DIR}/cmake/CompilerWarnings.cmake)
orpheus_enable_warnings(orpheus_perf_render_click)
