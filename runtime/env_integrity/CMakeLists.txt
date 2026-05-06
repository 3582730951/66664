add_library(vmp_runtime_env_integrity STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src/monitor.cpp)
target_link_libraries(vmp_runtime_env_integrity PUBLIC
  vmp_build_options
  vmp_runtime_audit
  vmp_runtime_strings
  vmp_runtime_trusted_oracle)
target_include_directories(vmp_runtime_env_integrity PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include)
