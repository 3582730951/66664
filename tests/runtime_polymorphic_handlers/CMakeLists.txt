function(vmp_add_runtime_polymorphic_handler_test target source)
  add_executable(${target} ${source})
  target_link_libraries(${target} PRIVATE vmp_runtime_vm1 vmp_runtime_vm2 vmp_runtime_strings vmp_runtime_integrity vmp_runtime_audit)
  set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
  add_test(NAME ${target} COMMAND $<TARGET_FILE:${target}>)
  set_tests_properties(${target} PROPERTIES PASS_REGULAR_EXPRESSION "${target} OK")
endfunction()

vmp_add_runtime_polymorphic_handler_test(polymorphic_vm1_layout
  ${CMAKE_CURRENT_SOURCE_DIR}/polymorphic_vm1_layout.cpp)
vmp_add_runtime_polymorphic_handler_test(polymorphic_vm2_layout
  ${CMAKE_CURRENT_SOURCE_DIR}/polymorphic_vm2_layout.cpp)

add_library(vmp_poly_build_options_seed_a INTERFACE)
target_compile_features(vmp_poly_build_options_seed_a INTERFACE cxx_std_17)
target_compile_definitions(vmp_poly_build_options_seed_a INTERFACE
  VMP_WITH_VM2=$<BOOL:${VMP_WITH_VM2}>
  VMP_PLATFORM_STR="${VMP_PLATFORM}"
  VMP_ARCH_STR="${VMP_ARCH}")
if(MSVC)
  target_compile_options(vmp_poly_build_options_seed_a INTERFACE /Oy-)
else()
  target_compile_options(vmp_poly_build_options_seed_a INTERFACE -fno-omit-frame-pointer)
endif()

add_library(vmp_poly_build_options_seed_b INTERFACE)
target_compile_features(vmp_poly_build_options_seed_b INTERFACE cxx_std_17)
target_compile_definitions(vmp_poly_build_options_seed_b INTERFACE
  VMP_WITH_VM2=$<BOOL:${VMP_WITH_VM2}>
  VMP_PLATFORM_STR="${VMP_PLATFORM}"
  VMP_ARCH_STR="${VMP_ARCH}")
if(MSVC)
  target_compile_options(vmp_poly_build_options_seed_b INTERFACE /Oy-)
else()
  target_compile_options(vmp_poly_build_options_seed_b INTERFACE -fno-omit-frame-pointer)
endif()

function(vmp_add_seeded_runtime_vm_pair suffix seed_options_target seed_value)
  add_library(vmp_runtime_vm1_${suffix} STATIC
    ${CMAKE_SOURCE_DIR}/runtime/vm1/src/vm1.cpp
    ${CMAKE_SOURCE_DIR}/runtime/vm1/src/interpreter.cpp
    ${CMAKE_SOURCE_DIR}/runtime/vm1/src/bridge.cpp)
  target_link_libraries(vmp_runtime_vm1_${suffix} PUBLIC ${seed_options_target} vmp_runtime_audit vmp_runtime_strings vmp_runtime_cryptor vmp_runtime_obfuscation)
  target_link_libraries(vmp_runtime_vm1_${suffix} PUBLIC vmp_runtime_env_integrity vmp_runtime_stack_probe)
  target_link_libraries(vmp_runtime_vm1_${suffix} PRIVATE vmp_arch_common)
  target_compile_definitions(vmp_runtime_vm1_${suffix} PRIVATE
    VMP_WITH_JIT=0
    VMP_POLYMORPHIC_HANDLER_SEED=${seed_value})
  target_include_directories(vmp_runtime_vm1_${suffix} PUBLIC
    ${CMAKE_SOURCE_DIR}/runtime/vm1/include
    ${CMAKE_SOURCE_DIR}/arch/common/include
    ${CMAKE_SOURCE_DIR}/runtime/bridge/include
    ${CMAKE_SOURCE_DIR}/runtime/cryptor/include
    ${CMAKE_SOURCE_DIR}/runtime/env_integrity/include
    ${CMAKE_SOURCE_DIR}/runtime/integrity/include
    ${CMAKE_SOURCE_DIR}/runtime/jit/include
    ${CMAKE_SOURCE_DIR}/runtime/obfuscation/include
    ${CMAKE_SOURCE_DIR}/runtime/state/include
    ${CMAKE_SOURCE_DIR}/runtime/stack_probe/include)

  add_library(vmp_runtime_vm2_${suffix} STATIC
    ${CMAKE_SOURCE_DIR}/runtime/vm2/src/vm2.cpp
    ${CMAKE_SOURCE_DIR}/runtime/vm2/src/interpreter.cpp)
  target_link_libraries(vmp_runtime_vm2_${suffix} PUBLIC ${seed_options_target} vmp_runtime_audit vmp_runtime_strings vmp_runtime_vm1_${suffix} vmp_runtime_cryptor vmp_runtime_obfuscation)
  target_link_libraries(vmp_runtime_vm2_${suffix} PUBLIC vmp_runtime_env_integrity vmp_runtime_stack_probe)
  target_compile_definitions(vmp_runtime_vm2_${suffix} PRIVATE
    VMP_WITH_JIT=0
    VMP_POLYMORPHIC_HANDLER_SEED=${seed_value})
  target_include_directories(vmp_runtime_vm2_${suffix} PUBLIC
    ${CMAKE_SOURCE_DIR}/runtime/vm2/include
    ${CMAKE_SOURCE_DIR}/runtime/vm1/include
    ${CMAKE_SOURCE_DIR}/runtime/bridge/include
    ${CMAKE_SOURCE_DIR}/runtime/cryptor/include
    ${CMAKE_SOURCE_DIR}/runtime/env_integrity/include
    ${CMAKE_SOURCE_DIR}/runtime/integrity/include
    ${CMAKE_SOURCE_DIR}/runtime/jit/include
    ${CMAKE_SOURCE_DIR}/runtime/obfuscation/include
    ${CMAKE_SOURCE_DIR}/runtime/state/include
    ${CMAKE_SOURCE_DIR}/runtime/stack_probe/include)

  add_executable(polymorphic_handler_dump_${suffix} ${CMAKE_CURRENT_SOURCE_DIR}/polymorphic_handler_dump.cpp)
  target_link_libraries(polymorphic_handler_dump_${suffix} PRIVATE
    vmp_runtime_vm1_${suffix} vmp_runtime_vm2_${suffix}
    vmp_runtime_audit vmp_runtime_strings vmp_runtime_integrity vmp_runtime_jit
    nlohmann_json::nlohmann_json)
  set_target_properties(polymorphic_handler_dump_${suffix} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
endfunction()

vmp_add_seeded_runtime_vm_pair(seed_a vmp_poly_build_options_seed_a 1111111)
vmp_add_seeded_runtime_vm_pair(seed_b vmp_poly_build_options_seed_b 2222222)

add_test(NAME polymorphic_handler_seed_divergence
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/polymorphic_handler_seed_divergence.py
          $<TARGET_FILE:polymorphic_handler_dump_seed_a>
          $<TARGET_FILE:polymorphic_handler_dump_seed_b>)
set_tests_properties(polymorphic_handler_seed_divergence PROPERTIES PASS_REGULAR_EXPRESSION "polymorphic_handler_seed_divergence OK")
