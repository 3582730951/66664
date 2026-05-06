function(vmp_add_runtime_stack_probe_test target source)
  add_executable(${target} ${source})
  target_link_libraries(${target} PRIVATE
    vmp_runtime_stack_probe
    vmp_runtime_trampoline
    vmp_runtime_vm1
    vmp_runtime_vm2
    vmp_runtime_trusted_oracle
    vmp_runtime_strings
    vmp_runtime_audit)
  set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
  add_test(NAME ${target} COMMAND $<TARGET_FILE:${target}>)
  set_tests_properties(${target} PROPERTIES PASS_REGULAR_EXPRESSION "${target} OK")
endfunction()

vmp_add_runtime_stack_probe_test(stack_probe_trigger_gate_and_anon_exec_event
  ${CMAKE_CURRENT_SOURCE_DIR}/stack_probe_trigger_gate_and_anon_exec_event.cpp)

vmp_add_runtime_stack_probe_test(stack_probe_libunwind_fallback_deleted_mapping
  ${CMAKE_CURRENT_SOURCE_DIR}/stack_probe_libunwind_fallback_deleted_mapping.cpp)

vmp_add_runtime_stack_probe_test(stack_probe_integration_sites
  ${CMAKE_CURRENT_SOURCE_DIR}/stack_probe_integration_sites.cpp)

vmp_add_runtime_stack_probe_test(stack_probe_selector_low16_and_maps_cache
  ${CMAKE_CURRENT_SOURCE_DIR}/stack_probe_selector_low16_and_maps_cache.cpp)
