function(vmp_add_runtime_env_integrity_test target source)
  add_executable(${target} ${source})
  target_link_libraries(${target} PRIVATE
    vmp_runtime_env_integrity
    vmp_runtime_trusted_oracle
    vmp_runtime_trampoline
    vmp_runtime_vm1
    vmp_runtime_vm2
    vmp_runtime_audit
    vmp_runtime_strings
    vmp_policy
    nlohmann_json::nlohmann_json)
  target_include_directories(${target} PRIVATE
    ${CMAKE_SOURCE_DIR}/tests/runtime_vm1
    ${CMAKE_SOURCE_DIR}/tests/runtime_vm2)
  set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
  add_test(NAME ${target} COMMAND $<TARGET_FILE:${target}>)
  set_tests_properties(${target} PROPERTIES PASS_REGULAR_EXPRESSION "${target} OK")
endfunction()

vmp_add_runtime_env_integrity_test(env_exception_baseline_detects_new_handler
  ${CMAKE_CURRENT_SOURCE_DIR}/env_exception_baseline_detects_new_handler.cpp)
vmp_add_runtime_env_integrity_test(env_exception_whitelist_ignores_untracked_signal
  ${CMAKE_CURRENT_SOURCE_DIR}/env_exception_whitelist_ignores_untracked_signal.cpp)
vmp_add_runtime_env_integrity_test(env_got_baseline_detects_tamper
  ${CMAKE_CURRENT_SOURCE_DIR}/env_got_baseline_detects_tamper.cpp)
vmp_add_runtime_env_integrity_test(env_vm_sensitive_domain_entry_checks_monitor
  ${CMAKE_CURRENT_SOURCE_DIR}/env_vm_sensitive_domain_entry_checks_monitor.cpp)
