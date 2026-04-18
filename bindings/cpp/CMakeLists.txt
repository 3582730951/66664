add_library(vmp_bindings_cpp STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src/plugin.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/fallback_scanner.cpp)
target_link_libraries(vmp_bindings_cpp PUBLIC vmp_policy vmp_build_options)
target_include_directories(vmp_bindings_cpp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(vmp-cpp-fallback-scan ${CMAKE_CURRENT_SOURCE_DIR}/src/fallback_scanner_main.cpp)
target_link_libraries(vmp-cpp-fallback-scan PRIVATE vmp_bindings_cpp)
set_target_properties(vmp-cpp-fallback-scan PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tools)

set(VMP_BINDINGS_CPP_HAVE_CLANG_PLUGIN OFF PARENT_SCOPE)
set(VMP_BINDINGS_CPP_PLUGIN_TARGET "" PARENT_SCOPE)
set(VMP_BINDINGS_CPP_FALLBACK_TOOL_TARGET vmp-cpp-fallback-scan PARENT_SCOPE)
set(VMP_BINDINGS_CPP_AST_TOOL_TARGET "" PARENT_SCOPE)

set(_vmp_bindings_cpp_enable_clang_plugin OFF)
if(NOT WIN32)
  set(_vmp_clang_probe_script ${CMAKE_CURRENT_BINARY_DIR}/probe_clang_targets.cmake)
  file(WRITE ${_vmp_clang_probe_script} [=[
find_package(LLVM CONFIG QUIET)
if(NOT LLVM_FOUND)
  message(FATAL_ERROR "LLVM not found")
endif()
find_package(Clang CONFIG QUIET)
if(NOT Clang_FOUND)
  message(FATAL_ERROR "Clang not found")
endif()
foreach(_target IN ITEMS clangTooling clangAST clangBasic clangFrontend clangLex)
  if(NOT TARGET ${_target})
    message(FATAL_ERROR "missing target: ${_target}")
  endif()
endforeach()
if(NOT TARGET LLVM AND NOT TARGET LLVMCore)
  message(FATAL_ERROR "missing LLVM imported target")
endif()
]=])
  execute_process(
    COMMAND ${CMAKE_COMMAND} -P ${_vmp_clang_probe_script}
    RESULT_VARIABLE _vmp_clang_probe_result
    OUTPUT_QUIET
    ERROR_QUIET)
  if(_vmp_clang_probe_result EQUAL 0)
    find_package(LLVM CONFIG QUIET)
    find_package(Clang CONFIG QUIET)
    foreach(_target IN ITEMS clangTooling clangAST clangBasic clangFrontend clangLex)
      if(NOT TARGET ${_target})
        set(_vmp_clang_probe_result 1)
      endif()
    endforeach()
    if((TARGET LLVM OR TARGET LLVMCore) AND _vmp_clang_probe_result EQUAL 0)
      set(_vmp_bindings_cpp_enable_clang_plugin ON)
    endif()
  endif()
endif()

if(_vmp_bindings_cpp_enable_clang_plugin)
  message(STATUS "bindings/cpp: enabling clang plugin collector")

  add_executable(vmp-cpp-clang-collect ${CMAKE_CURRENT_SOURCE_DIR}/clang_plugin/vmp_annotate_tool.cpp)
  target_include_directories(vmp-cpp-clang-collect PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${LLVM_INCLUDE_DIRS} ${CLANG_INCLUDE_DIRS})
  target_link_libraries(vmp-cpp-clang-collect PRIVATE
    vmp_policy
    clangTooling
    clangAST
    clangBasic
    clangFrontend
    clangLex
    $<$<TARGET_EXISTS:LLVM>:LLVM>
    $<$<AND:$<NOT:$<TARGET_EXISTS:LLVM>>,$<TARGET_EXISTS:LLVMCore>>:LLVMCore>)
  target_compile_options(vmp-cpp-clang-collect PRIVATE ${VMP_LLVM_DEFINITIONS})
  set_target_properties(vmp-cpp-clang-collect PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tools)

  add_library(vmp_annotate_plugin MODULE ${CMAKE_CURRENT_SOURCE_DIR}/clang_plugin/vmp_annotate_plugin.cpp)
  target_include_directories(vmp_annotate_plugin PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${LLVM_INCLUDE_DIRS} ${CLANG_INCLUDE_DIRS})
  target_link_libraries(vmp_annotate_plugin PRIVATE
    vmp_policy
    clangAST
    clangBasic
    clangFrontend
    clangLex
    $<$<TARGET_EXISTS:LLVM>:LLVM>
    $<$<AND:$<NOT:$<TARGET_EXISTS:LLVM>>,$<TARGET_EXISTS:LLVMCore>>:LLVMCore>)
  target_compile_options(vmp_annotate_plugin PRIVATE ${VMP_LLVM_DEFINITIONS})
  target_compile_definitions(vmp_bindings_cpp PUBLIC VMP_BINDINGS_CPP_HAVE_CLANG_PLUGIN=1)
  set_target_properties(vmp_annotate_plugin PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tools
    PREFIX "")
  set(VMP_BINDINGS_CPP_HAVE_CLANG_PLUGIN ON PARENT_SCOPE)
  set(VMP_BINDINGS_CPP_PLUGIN_TARGET vmp_annotate_plugin PARENT_SCOPE)
  set(VMP_BINDINGS_CPP_AST_TOOL_TARGET vmp-cpp-clang-collect PARENT_SCOPE)
else()
  message(STATUS "bindings/cpp: clang plugin collector unavailable, using fallback scanner only")
endif()
