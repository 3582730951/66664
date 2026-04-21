function(vmp_add_runtime_timing_trap_test target source)
  add_executable(${target} ${source})
  target_link_libraries(${target} PRIVATE
    vmp_runtime_vm1
    vmp_runtime_vm2
    vmp_runtime_obfuscation
    vmp_runtime_audit
    vmp_runtime_strings)
  set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
  add_test(NAME ${target} COMMAND $<TARGET_FILE:${target}>)
  set_tests_properties(${target} PROPERTIES PASS_REGULAR_EXPRESSION "${target} OK")
endfunction()

vmp_add_runtime_timing_trap_test(timing_trap_profile_roundtrip
  ${CMAKE_CURRENT_SOURCE_DIR}/timing_trap_profile_roundtrip.cpp)

vmp_add_runtime_timing_trap_test(timing_trap_vm1_baseline_ok
  ${CMAKE_CURRENT_SOURCE_DIR}/timing_trap_vm1_baseline_ok.cpp)

vmp_add_runtime_timing_trap_test(timing_trap_vm1_delay_siren
  ${CMAKE_CURRENT_SOURCE_DIR}/timing_trap_vm1_delay_siren.cpp)

vmp_add_runtime_timing_trap_test(timing_trap_vm2_fast_siren
  ${CMAKE_CURRENT_SOURCE_DIR}/timing_trap_vm2_fast_siren.cpp)
