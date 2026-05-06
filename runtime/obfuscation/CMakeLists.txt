add_library(vmp_runtime_obfuscation STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src/bogus_flow.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/mba.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/opaque.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/timing_trap.cpp)
target_link_libraries(vmp_runtime_obfuscation PUBLIC vmp_build_options vmp_runtime_audit vmp_runtime_strings)
target_include_directories(vmp_runtime_obfuscation PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_SOURCE_DIR}/runtime/audit/include
  ${CMAKE_SOURCE_DIR}/runtime/strings/include
  ${CMAKE_SOURCE_DIR}/runtime/vm1/include
  ${CMAKE_SOURCE_DIR}/runtime/vm2/include)
