if(NOT DEFINED source_dir OR NOT DEFINED binary_dir OR NOT DEFINED OrpheusSDK_DIR)
  message(FATAL_ERROR "Missing variables for find_package smoke test")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -S "${source_dir}" -B "${binary_dir}" -DOrpheusSDK_DIR:PATH=${OrpheusSDK_DIR}
  RESULT_VARIABLE configure_result)
if(configure_result)
  message(FATAL_ERROR "Configure failed with code ${configure_result}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${binary_dir}"
  RESULT_VARIABLE build_result)
if(build_result)
  message(FATAL_ERROR "Build failed with code ${build_result}")
endif()
