add_library(vmp_runtime_stack_probe STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src/probe.cpp)

target_link_libraries(vmp_runtime_stack_probe PUBLIC
  vmp_build_options
  vmp_runtime_audit
  vmp_runtime_trusted_oracle)

target_include_directories(vmp_runtime_stack_probe PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include)
