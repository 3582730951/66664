add_library(vmp_runtime_env_detectors STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src/detectors.cpp)

target_link_libraries(vmp_runtime_env_detectors PUBLIC
  vmp_build_options
  vmp_runtime_audit
  vmp_runtime_strings
  vmp_runtime_trusted_oracle)

target_include_directories(vmp_runtime_env_detectors PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include)
