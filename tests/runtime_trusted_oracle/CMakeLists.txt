function(vmp_add_runtime_trusted_oracle_test target source)
  add_executable(${target} ${source})
  target_link_libraries(${target} PRIVATE
    vmp_runtime_trusted_oracle
    vmp_runtime_strings
    vmp_runtime_audit
    vmp_policy
    nlohmann_json::nlohmann_json)
  set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
  add_test(NAME ${target} COMMAND $<TARGET_FILE:${target}>)
  set_tests_properties(${target} PROPERTIES PASS_REGULAR_EXPRESSION "${target} OK")
endfunction()

vmp_add_runtime_trusted_oracle_test(trusted_oracle_direct_syscall_proc_status
  ${CMAKE_CURRENT_SOURCE_DIR}/trusted_oracle_direct_syscall_proc_status.cpp)
vmp_add_runtime_trusted_oracle_test(trusted_oracle_prologue_baseline_detects_tamper
  ${CMAKE_CURRENT_SOURCE_DIR}/trusted_oracle_prologue_baseline_detects_tamper.cpp)
vmp_add_runtime_trusted_oracle_test(trusted_oracle_cross_source_voting
  ${CMAKE_CURRENT_SOURCE_DIR}/trusted_oracle_cross_source_voting.cpp)
vmp_add_runtime_trusted_oracle_test(trusted_oracle_thread_liveness
  ${CMAKE_CURRENT_SOURCE_DIR}/trusted_oracle_thread_liveness.cpp)
vmp_add_runtime_trusted_oracle_test(trusted_oracle_single_source_ablation
  ${CMAKE_CURRENT_SOURCE_DIR}/trusted_oracle_single_source_ablation.cpp)

add_executable(trusted_oracle_ablation_matrix
  ${CMAKE_CURRENT_SOURCE_DIR}/trusted_oracle_ablation_matrix.cpp)
target_link_libraries(trusted_oracle_ablation_matrix PRIVATE
  vmp_runtime_trusted_oracle
  vmp_runtime_strings
  vmp_runtime_audit
  vmp_policy
  nlohmann_json::nlohmann_json)
set_target_properties(trusted_oracle_ablation_matrix PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
