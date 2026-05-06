function(vmp_add_runtime_rolling_opcode_test target source)
  add_executable(${target} ${source})
  target_link_libraries(${target} PRIVATE
    vmp_runtime_cryptor
    vmp_runtime_vm1
    vmp_runtime_vm2
    vmp_runtime_jit
    vmp_runtime_strings
    vmp_runtime_audit
    vmp_runtime_integrity
    vmp_runtime_state
    vmp_policy
    nlohmann_json::nlohmann_json)
  set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
  add_test(NAME ${target} COMMAND $<TARGET_FILE:${target}>)
  set_tests_properties(${target} PROPERTIES PASS_REGULAR_EXPRESSION "${target} OK")
endfunction()

vmp_add_runtime_rolling_opcode_test(rolling_opcode_store_current_previous
  ${CMAKE_CURRENT_SOURCE_DIR}/rolling_opcode_store_current_previous.cpp)
vmp_add_runtime_rolling_opcode_test(rolling_opcode_previous_epoch_inflight_fetch
  ${CMAKE_CURRENT_SOURCE_DIR}/rolling_opcode_previous_epoch_inflight_fetch.cpp)
vmp_add_runtime_rolling_opcode_test(rolling_opcode_vm1_event_rotation
  ${CMAKE_CURRENT_SOURCE_DIR}/rolling_opcode_vm1_event_rotation.cpp)
vmp_add_runtime_rolling_opcode_test(rolling_opcode_dispatch_budget_hot_path
  ${CMAKE_CURRENT_SOURCE_DIR}/rolling_opcode_dispatch_budget_hot_path.cpp)
vmp_add_runtime_rolling_opcode_test(rolling_opcode_domain_switch_rotates_both_domains
  ${CMAKE_CURRENT_SOURCE_DIR}/rolling_opcode_domain_switch_rotates_both_domains.cpp)
vmp_add_runtime_rolling_opcode_test(rolling_opcode_vm1_jit_evicts_on_epoch_bump
  ${CMAKE_CURRENT_SOURCE_DIR}/rolling_opcode_vm1_jit_evicts_on_epoch_bump.cpp)
vmp_add_runtime_rolling_opcode_test(rolling_opcode_vm2_jit_epoch_tag_evict
  ${CMAKE_CURRENT_SOURCE_DIR}/rolling_opcode_vm2_jit_epoch_tag_evict.cpp)

vmp_add_runtime_rolling_opcode_test(rolling_opcode_lazy_page_materialization
  ${CMAKE_CURRENT_SOURCE_DIR}/rolling_opcode_lazy_page_materialization.cpp)
