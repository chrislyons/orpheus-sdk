add_library(orpheus_core
  core/abi.cpp
  core/session/session_graph.cpp
  core/session/json_io.cpp
)

set_target_properties(orpheus_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(BUILD_SHARED_LIBS)
  target_compile_definitions(orpheus_core
    PUBLIC ORPHEUS_BUILD_SHARED
    PRIVATE ORPHEUS_BUILDING_SHARED)
  if(NOT MSVC)
    set_target_properties(orpheus_core PROPERTIES
      CXX_VISIBILITY_PRESET hidden
      VISIBILITY_INLINES_HIDDEN YES)
  endif()
endif()

target_include_directories(orpheus_core
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(orpheus_core PUBLIC cxx_std_20)

include(${CMAKE_SOURCE_DIR}/cmake/CompilerWarnings.cmake)
orpheus_enable_warnings(orpheus_core)
