function(vmp_add_runtime_dispatcher_hardening_test target source)
  add_executable(${target} ${source})
  target_link_libraries(${target} PRIVATE vmp_runtime_trampoline vmp_runtime_trusted_oracle vmp_runtime_strings vmp_runtime_audit)
  set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
  add_test(NAME ${target} COMMAND $<TARGET_FILE:${target}>)
  set_tests_properties(${target} PROPERTIES PASS_REGULAR_EXPRESSION "${target} OK")
endfunction()

vmp_add_runtime_dispatcher_hardening_test(dispatcher_self_hash_detects_tamper
  ${CMAKE_CURRENT_SOURCE_DIR}/dispatcher_self_hash_detects_tamper.cpp)

vmp_add_runtime_dispatcher_hardening_test(dispatcher_target_prologue_and_replay
  ${CMAKE_CURRENT_SOURCE_DIR}/dispatcher_target_prologue_and_replay.cpp)

vmp_add_runtime_dispatcher_hardening_test(dispatcher_ephemeral_hmac_derivation
  ${CMAKE_CURRENT_SOURCE_DIR}/dispatcher_ephemeral_hmac_derivation.cpp)

vmp_add_runtime_dispatcher_hardening_test(dispatcher_batch_hmac_epoch_cache
  ${CMAKE_CURRENT_SOURCE_DIR}/dispatcher_batch_hmac_epoch_cache.cpp)
