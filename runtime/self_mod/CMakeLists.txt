add_library(vmp_runtime_self_mod STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src/mutation.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/interlock.cpp)

target_link_libraries(vmp_runtime_self_mod PUBLIC
  vmp_build_options
  vmp_runtime_audit
  vmp_runtime_integrity
  vmp_runtime_strings)

target_include_directories(vmp_runtime_self_mod PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_SOURCE_DIR}/runtime/audit/include
  ${CMAKE_SOURCE_DIR}/runtime/cryptor/include
  ${CMAKE_SOURCE_DIR}/runtime/integrity/include
  ${CMAKE_SOURCE_DIR}/runtime/strings/include
  ${CMAKE_SOURCE_DIR}/runtime/vm1/include
  ${CMAKE_SOURCE_DIR}/runtime/vm2/include)
