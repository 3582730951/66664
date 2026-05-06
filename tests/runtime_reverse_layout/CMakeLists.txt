set(VMP_RUNTIME_REVERSE_LAYOUT_FIXTURES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

function(vmp_add_runtime_reverse_layout_test target source)
  add_executable(${target} ${source})
  target_link_libraries(${target} PRIVATE vmp_runtime_vm1 vmp_runtime_vm2 vmp_runtime_strings vmp_runtime_integrity vmp_runtime_audit)
  target_compile_definitions(${target} PRIVATE
    VMP_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    VMP_RUNTIME_REVERSE_LAYOUT_FIXTURES_DIR="${VMP_RUNTIME_REVERSE_LAYOUT_FIXTURES_DIR}")
  set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
  add_test(NAME ${target} COMMAND $<TARGET_FILE:${target}>)
  set_tests_properties(${target} PROPERTIES PASS_REGULAR_EXPRESSION "${target} OK")
endfunction()

vmp_add_runtime_reverse_layout_test(reverse_layout_fib20_equivalent
  ${CMAKE_CURRENT_SOURCE_DIR}/reverse_layout_fib20_equivalent.cpp)
vmp_add_runtime_reverse_layout_test(reverse_layout_branches_forward_pc
  ${CMAKE_CURRENT_SOURCE_DIR}/reverse_layout_branches_forward_pc.cpp)
vmp_add_runtime_reverse_layout_test(reverse_layout_length_table_derivable
  ${CMAKE_CURRENT_SOURCE_DIR}/reverse_layout_length_table_derivable.cpp)
vmp_add_runtime_reverse_layout_test(reverse_layout_vm2_equivalent
  ${CMAKE_CURRENT_SOURCE_DIR}/reverse_layout_vm2_equivalent.cpp)

add_test(NAME reverse_layout_crc32_valid
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/reverse_layout_crc32_valid.py)
set_tests_properties(reverse_layout_crc32_valid PROPERTIES
  ENVIRONMENT "VMP_TEST_BUILD_DIR=${CMAKE_BINARY_DIR}"
  PASS_REGULAR_EXPRESSION "reverse_layout_crc32_valid OK")

add_test(NAME reverse_layout_with_cryptor
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/reverse_layout_with_cryptor.py)
set_tests_properties(reverse_layout_with_cryptor PROPERTIES
  ENVIRONMENT "VMP_TEST_BUILD_DIR=${CMAKE_BINARY_DIR}"
  PASS_REGULAR_EXPRESSION "reverse_layout_with_cryptor OK")

add_test(NAME vmp_vm1_layout_cli
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/vmp_vm1_layout_cli.py)
set_tests_properties(vmp_vm1_layout_cli PROPERTIES
  ENVIRONMENT "VMP_TEST_BUILD_DIR=${CMAKE_BINARY_DIR}"
  PASS_REGULAR_EXPRESSION "vmp_vm1_layout_cli OK")

add_test(NAME vmp_vm2_layout_cli
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/vmp_vm2_layout_cli.py)
set_tests_properties(vmp_vm2_layout_cli PROPERTIES
  ENVIRONMENT "VMP_TEST_BUILD_DIR=${CMAKE_BINARY_DIR}"
  PASS_REGULAR_EXPRESSION "vmp_vm2_layout_cli OK")
