add_library(vmp_runtime_jit STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src/jit.cpp)
target_link_libraries(vmp_runtime_jit PUBLIC vmp_build_options vmp_runtime_audit vmp_runtime_strings)
target_include_directories(vmp_runtime_jit PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_SOURCE_DIR}/runtime/vm1/include
  ${CMAKE_SOURCE_DIR}/runtime/strings/include)
