function(vmp_add_runtime_crypto_revision_test target source)
  add_executable(${target} ${source})
  target_link_libraries(${target} PRIVATE
    vmp_runtime_trampoline
    vmp_runtime_strings
    vmp_runtime_cryptor
    vmp_runtime_vm1
    vmp_runtime_vm2
    vmp_runtime_jit
    vmp_runtime_integrity
    vmp_runtime_audit
    vmp_runtime_state
    vmp_policy
    nlohmann_json::nlohmann_json)
  target_compile_definitions(${target} PRIVATE VMP_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
  add_test(NAME ${target} COMMAND $<TARGET_FILE:${target}>)
  set_tests_properties(${target} PROPERTIES PASS_REGULAR_EXPRESSION "${target} OK")
endfunction()

vmp_add_runtime_crypto_revision_test(crypto_revision_token_bundle_compat
  ${CMAKE_CURRENT_SOURCE_DIR}/crypto_revision_token_bundle_compat.cpp)

vmp_add_runtime_crypto_revision_test(crypto_revision_trampoline_x64_128bit
  ${CMAKE_CURRENT_SOURCE_DIR}/crypto_revision_trampoline_x64_128bit.cpp)

vmp_add_runtime_crypto_revision_test(crypto_revision_hkdf_namespaces
  ${CMAKE_CURRENT_SOURCE_DIR}/crypto_revision_hkdf_namespaces.cpp)

vmp_add_runtime_crypto_revision_test(crypto_revision_constant_time_hmac
  ${CMAKE_CURRENT_SOURCE_DIR}/crypto_revision_constant_time_hmac.cpp)
