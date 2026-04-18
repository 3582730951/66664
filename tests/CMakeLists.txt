find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(VMP_TEST_BIN_SUFFIX "")
if(WIN32)
  set(VMP_TEST_BIN_SUFFIX ".exe")
endif()

set(VMP_POLICY_FIXTURES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/policy/examples")
set(VMP_AUDIT_FIXTURES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/audit")
set(VMP_BINDINGS_CPP_FIXTURES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/bindings_cpp")

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/dummy_test.py ${CMAKE_CURRENT_BINARY_DIR}/dummy_test.py COPYONLY)

add_executable(policy_cpp_test ${CMAKE_CURRENT_SOURCE_DIR}/policy/policy_cpp_test.cpp)
target_link_libraries(policy_cpp_test PRIVATE vmp_policy)
target_compile_definitions(policy_cpp_test PRIVATE
  VMP_POLICY_FIXTURES_DEFAULT_DIR="${VMP_POLICY_FIXTURES_DIR}")

add_executable(policy_cpp_summary ${CMAKE_CURRENT_SOURCE_DIR}/policy/policy_cpp_summary.cpp)
target_link_libraries(policy_cpp_summary PRIVATE vmp_policy)
target_compile_definitions(policy_cpp_summary PRIVATE
  VMP_POLICY_FIXTURES_DEFAULT_DIR="${VMP_POLICY_FIXTURES_DIR}")

add_test(NAME dummy_python_smoke
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/dummy_test.py)

add_test(NAME policy_cpp_test
  COMMAND policy_cpp_test)
set_tests_properties(policy_cpp_test PROPERTIES
  ENVIRONMENT "VMP_POLICY_FIXTURES_DIR=${VMP_POLICY_FIXTURES_DIR}")

add_test(NAME vmp_protect_good_policy
  COMMAND $<TARGET_FILE:vmp-protect> --policy ${CMAKE_CURRENT_SOURCE_DIR}/policy/examples/good.json)
set_tests_properties(vmp_protect_good_policy PROPERTIES PASS_REGULAR_EXPRESSION "OK: policy loaded, 2 entries, schema=v1")

add_test(NAME vmp_protect_bad_policy
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/policy/assert_bad_policy.py $<TARGET_FILE:vmp-protect> ${CMAKE_CURRENT_SOURCE_DIR}/policy/examples/bad_vm_func_native.json)
set_tests_properties(vmp_protect_bad_policy PROPERTIES PASS_REGULAR_EXPRESSION "bad policy rejected as expected")

add_test(NAME vmp_protect_dump_schema
  COMMAND $<TARGET_FILE:vmp-protect> --dump-schema)
set_tests_properties(vmp_protect_dump_schema PROPERTIES PASS_REGULAR_EXPRESSION "language_origin")

add_executable(audit_cpp_test ${CMAKE_CURRENT_SOURCE_DIR}/audit/audit_cpp_test.cpp)
target_link_libraries(audit_cpp_test PRIVATE vmp_runtime_audit nlohmann_json::nlohmann_json)
target_compile_definitions(audit_cpp_test PRIVATE
  VMP_AUDIT_FIXTURES_DEFAULT_DIR="${VMP_AUDIT_FIXTURES_DIR}")
set_target_properties(audit_cpp_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)

add_executable(audit_cpp_format ${CMAKE_CURRENT_SOURCE_DIR}/audit/audit_cpp_format.cpp)
target_link_libraries(audit_cpp_format PRIVATE vmp_runtime_audit nlohmann_json::nlohmann_json)
set_target_properties(audit_cpp_format PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)

add_test(NAME audit_cpp_test
  COMMAND $<TARGET_FILE:audit_cpp_test>)
set_tests_properties(audit_cpp_test PROPERTIES PASS_REGULAR_EXPRESSION "audit_cpp_test OK")

add_test(NAME cross_language_golden
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/audit/compare_cpp_rust_audit.py $<TARGET_FILE:audit_cpp_format> ${CMAKE_CURRENT_SOURCE_DIR}/audit/golden_record.json ${CMAKE_SOURCE_DIR})
set_tests_properties(cross_language_golden PROPERTIES
  PASS_REGULAR_EXPRESSION "cross language audit golden OK"
  SKIP_REGULAR_EXPRESSION "cross language audit golden SKIPPED")

add_test(NAME vmp_protect_detector_selftest
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/audit/assert_detector_selftest.py $<TARGET_FILE:vmp-protect> ${CMAKE_BINARY_DIR}/tests/detector_selftest.log)
set_tests_properties(vmp_protect_detector_selftest PROPERTIES PASS_REGULAR_EXPRESSION "detector selftest OK")

# bindings/cpp tests
set(VMP_CPP_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/bindings/cpp/include)
set(VMP_CPP_SAMPLE_C ${CMAKE_CURRENT_SOURCE_DIR}/bindings_cpp/sample_c.c)
set(VMP_CPP_SAMPLE_CPP ${CMAKE_CURRENT_SOURCE_DIR}/bindings_cpp/sample_cpp.cpp)
set(VMP_CPP_EXPECTED_C ${CMAKE_CURRENT_SOURCE_DIR}/bindings_cpp/expected_c.json)
set(VMP_CPP_EXPECTED_CPP_PLUGIN ${CMAKE_CURRENT_SOURCE_DIR}/bindings_cpp/expected_cpp_plugin.json)
set(VMP_CPP_EXPECTED_CPP_FALLBACK ${CMAKE_CURRENT_SOURCE_DIR}/bindings_cpp/expected_cpp_fallback.json)
set(VMP_CPP_COMPARE ${CMAKE_CURRENT_SOURCE_DIR}/bindings_cpp/compare_policy_json.py)
set(VMP_CPP_ASSERT_EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/bindings_cpp/assert_exists.py)

add_test(NAME bindings_cpp_collect_c
  COMMAND $<TARGET_FILE:vmp-clang> -c ${VMP_CPP_SAMPLE_C} -I ${VMP_CPP_INCLUDE_DIR} --vmp-collect=${CMAKE_BINARY_DIR}/tests/sample_c.policy.json -o ${CMAKE_BINARY_DIR}/tests/sample_c.o)
add_test(NAME bindings_cpp_collect_c_compare
  COMMAND ${Python3_EXECUTABLE} ${VMP_CPP_COMPARE} ${CMAKE_BINARY_DIR}/tests/sample_c.policy.json ${VMP_CPP_EXPECTED_C})
set_tests_properties(bindings_cpp_collect_c_compare PROPERTIES DEPENDS bindings_cpp_collect_c PASS_REGULAR_EXPRESSION "policy json equal")

add_test(NAME bindings_cpp_collect_cpp
  COMMAND $<TARGET_FILE:vmp-clang++> -std=c++17 -c ${VMP_CPP_SAMPLE_CPP} -I ${VMP_CPP_INCLUDE_DIR} --vmp-collect=${CMAKE_BINARY_DIR}/tests/sample_cpp.policy.json -o ${CMAKE_BINARY_DIR}/tests/sample_cpp.o)
if(VMP_BINDINGS_CPP_HAVE_CLANG_PLUGIN)
  add_test(NAME bindings_cpp_collect_cpp_compare
    COMMAND ${Python3_EXECUTABLE} ${VMP_CPP_COMPARE} ${CMAKE_BINARY_DIR}/tests/sample_cpp.policy.json ${VMP_CPP_EXPECTED_CPP_PLUGIN})
else()
  add_test(NAME bindings_cpp_collect_cpp_compare
    COMMAND ${Python3_EXECUTABLE} ${VMP_CPP_COMPARE} ${CMAKE_BINARY_DIR}/tests/sample_cpp.policy.json ${VMP_CPP_EXPECTED_CPP_FALLBACK})
endif()
set_tests_properties(bindings_cpp_collect_cpp_compare PROPERTIES DEPENDS bindings_cpp_collect_cpp PASS_REGULAR_EXPRESSION "policy json equal")

add_test(NAME bindings_cpp_collect_cpp_fallback
  COMMAND ${CMAKE_COMMAND} -E env VMP_DISABLE_CLANG_PLUGIN=1 $<TARGET_FILE:vmp-clang++> -std=c++17 -c ${VMP_CPP_SAMPLE_CPP} -I ${VMP_CPP_INCLUDE_DIR} --vmp-collect=${CMAKE_BINARY_DIR}/tests/sample_cpp_fallback.policy.json -o ${CMAKE_BINARY_DIR}/tests/sample_cpp_fallback.o)
add_test(NAME bindings_cpp_collect_cpp_fallback_compare
  COMMAND ${Python3_EXECUTABLE} ${VMP_CPP_COMPARE} ${CMAKE_BINARY_DIR}/tests/sample_cpp_fallback.policy.json ${VMP_CPP_EXPECTED_CPP_FALLBACK})
set_tests_properties(bindings_cpp_collect_cpp_fallback_compare PROPERTIES DEPENDS bindings_cpp_collect_cpp_fallback PASS_REGULAR_EXPRESSION "policy json equal")

add_test(NAME bindings_cpp_header_compile_clang_c
  COMMAND clang -c ${VMP_CPP_SAMPLE_C} -I ${VMP_CPP_INCLUDE_DIR} -o ${CMAKE_BINARY_DIR}/tests/header_clang_c.o)
add_test(NAME bindings_cpp_header_compile_clang_cpp
  COMMAND clang++ -std=c++17 -c ${VMP_CPP_SAMPLE_CPP} -I ${VMP_CPP_INCLUDE_DIR} -o ${CMAKE_BINARY_DIR}/tests/header_clang_cpp.o)
add_test(NAME bindings_cpp_header_compile_gcc_c
  COMMAND gcc -c ${VMP_CPP_SAMPLE_C} -I ${VMP_CPP_INCLUDE_DIR} -o ${CMAKE_BINARY_DIR}/tests/header_gcc_c.o)
add_test(NAME bindings_cpp_header_compile_gcc_cpp
  COMMAND g++ -std=c++17 -c ${VMP_CPP_SAMPLE_CPP} -I ${VMP_CPP_INCLUDE_DIR} -o ${CMAKE_BINARY_DIR}/tests/header_gcc_cpp.o)
if(MSVC)
  add_test(NAME bindings_cpp_header_compile_msvc_cpp
    COMMAND cl /nologo /std:c++17 /c ${VMP_CPP_SAMPLE_CPP} /I ${VMP_CPP_INCLUDE_DIR} /Fo${CMAKE_BINARY_DIR}/tests/header_msvc_cpp.obj)
endif()

add_test(NAME bindings_cpp_vmp_clang_passthrough
  COMMAND $<TARGET_FILE:vmp-clang> -c ${VMP_CPP_SAMPLE_C} -I ${VMP_CPP_INCLUDE_DIR} -o ${CMAKE_BINARY_DIR}/tests/passthrough_c.o)
add_test(NAME bindings_cpp_vmp_clang_passthrough_exists
  COMMAND ${Python3_EXECUTABLE} ${VMP_CPP_ASSERT_EXISTS} ${CMAKE_BINARY_DIR}/tests/passthrough_c.o)
set_tests_properties(bindings_cpp_vmp_clang_passthrough_exists PROPERTIES DEPENDS bindings_cpp_vmp_clang_passthrough PASS_REGULAR_EXPRESSION "exists:")
