function(vmp_add_runtime_trampoline_test target source)
  add_executable(${target} ${source})
  target_link_libraries(${target} PRIVATE vmp_runtime_trampoline vmp_runtime_strings vmp_runtime_integrity vmp_runtime_audit)
  target_compile_definitions(${target} PRIVATE VMP_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
  add_test(NAME ${target} COMMAND $<TARGET_FILE:${target}>)
  set_tests_properties(${target} PROPERTIES PASS_REGULAR_EXPRESSION "${target} OK")
endfunction()

vmp_add_runtime_trampoline_test(token_manager_hkdf_derive
  ${CMAKE_CURRENT_SOURCE_DIR}/token_manager_hkdf_derive.cpp)

vmp_add_runtime_trampoline_test(trampoline_bytes_all_arch
  ${CMAKE_CURRENT_SOURCE_DIR}/trampoline_bytes_all_arch.cpp)

vmp_add_runtime_trampoline_test(stack_function_table_audits_and_hmac
  ${CMAKE_CURRENT_SOURCE_DIR}/stack_function_table_audits_and_hmac.cpp)

vmp_add_runtime_trampoline_test(trampoline_bundle_roundtrip
  ${CMAKE_CURRENT_SOURCE_DIR}/trampoline_bundle_roundtrip.cpp)

add_test(NAME vmp_trampoline_inject_cli
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/vmp_trampoline_inject_cli.py $<TARGET_FILE:vmp-trampoline-inject> ${CMAKE_SOURCE_DIR} ${CMAKE_BINARY_DIR})
set_tests_properties(vmp_trampoline_inject_cli PROPERTIES PASS_REGULAR_EXPRESSION "vmp_trampoline_inject_cli OK")
