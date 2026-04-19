add_library(vmp_arch_x86 STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src/x86.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/decoder.cpp)
target_link_libraries(vmp_arch_x86 PUBLIC vmp_build_options vmp_policy vmp_arch_common vmp_runtime_vm1)
target_include_directories(vmp_arch_x86 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
