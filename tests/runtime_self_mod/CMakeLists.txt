function(vmp_add_runtime_self_mod_test target source)
  add_executable(${target} ${source})
  target_link_libraries(${target} PRIVATE
    vmp_runtime_vm1
    vmp_runtime_vm2
    vmp_runtime_self_mod
    vmp_runtime_strings
    vmp_runtime_integrity
    vmp_runtime_audit
    vmp_policy
    nlohmann_json::nlohmann_json)
  set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
  add_test(NAME ${target} COMMAND $<TARGET_FILE:${target}>)
  set_tests_properties(${target} PROPERTIES PASS_REGULAR_EXPRESSION "${target} OK")
endfunction()

vmp_add_runtime_self_mod_test(self_mod_vm1_runtime_fetch_mismatch
  ${CMAKE_CURRENT_SOURCE_DIR}/self_mod_vm1_runtime_fetch_mismatch.cpp)
vmp_add_runtime_self_mod_test(self_mod_vm1_hmac_divergence
  ${CMAKE_CURRENT_SOURCE_DIR}/self_mod_vm1_hmac_divergence.cpp)
vmp_add_runtime_self_mod_test(self_mod_vm2_interlock_checksum_mismatch
  ${CMAKE_CURRENT_SOURCE_DIR}/self_mod_vm2_interlock_checksum_mismatch.cpp)
