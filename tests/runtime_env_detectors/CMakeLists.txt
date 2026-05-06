function(vmp_add_runtime_env_detectors_test target source)
  add_executable(${target} ${source})
  target_link_libraries(${target} PRIVATE
    vmp_runtime_env_detectors
    vmp_runtime_trampoline
    vmp_runtime_trusted_oracle
    vmp_runtime_strings
    vmp_runtime_audit)
  set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
  add_test(NAME ${target} COMMAND $<TARGET_FILE:${target}>)
  set_tests_properties(${target} PROPERTIES PASS_REGULAR_EXPRESSION "${target} OK")
endfunction()

vmp_add_runtime_env_detectors_test(env_detectors_hardware_breakpoint_cross_check
  ${CMAKE_CURRENT_SOURCE_DIR}/env_detectors_hardware_breakpoint_cross_check.cpp)

vmp_add_runtime_env_detectors_test(env_detectors_frida_divergence
  ${CMAKE_CURRENT_SOURCE_DIR}/env_detectors_frida_divergence.cpp)

vmp_add_runtime_env_detectors_test(env_detectors_emulator_k_of_n
  ${CMAKE_CURRENT_SOURCE_DIR}/env_detectors_emulator_k_of_n.cpp)

vmp_add_runtime_env_detectors_test(env_detectors_lockstep_dispatcher_drift
  ${CMAKE_CURRENT_SOURCE_DIR}/env_detectors_lockstep_dispatcher_drift.cpp)

if(WIN32)
  add_executable(windows_debugger_detector_fixture
    ${CMAKE_CURRENT_SOURCE_DIR}/windows_debugger_detector_fixture.cpp)
  target_link_libraries(windows_debugger_detector_fixture PRIVATE
    vmp_runtime_audit)
  set_target_properties(windows_debugger_detector_fixture PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
endif()
