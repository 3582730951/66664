add_executable(vmp-clang ${CMAKE_CURRENT_SOURCE_DIR}/src/vmp_clang.cpp)
add_executable(vmp-clang++ ${CMAKE_CURRENT_SOURCE_DIR}/src/vmp_clangxx.cpp)
vmp_add_placeholder_tool(vmp-link ${CMAKE_CURRENT_SOURCE_DIR}/src/vmp_link.cpp)
vmp_add_placeholder_tool(vmp-protect ${CMAKE_CURRENT_SOURCE_DIR}/src/vmp_protect.cpp)

foreach(target IN ITEMS vmp-clang vmp-clang++)
  target_link_libraries(${target} PRIVATE vmp_build_options)
  set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tools)
  target_compile_definitions(${target} PRIVATE
    VMP_CPP_FALLBACK_SCANNER_PATH="$<TARGET_FILE:${VMP_BINDINGS_CPP_FALLBACK_TOOL_TARGET}>")
  if(VMP_BINDINGS_CPP_AST_TOOL_TARGET)
    target_compile_definitions(${target} PRIVATE
      VMP_CPP_COLLECTOR_TOOL_PATH="$<TARGET_FILE:${VMP_BINDINGS_CPP_AST_TOOL_TARGET}>")
  else()
    target_compile_definitions(${target} PRIVATE VMP_CPP_COLLECTOR_TOOL_PATH="")
  endif()
  if(VMP_BINDINGS_CPP_HAVE_CLANG_PLUGIN)
    target_compile_definitions(${target} PRIVATE
      VMP_CPP_PLUGIN_PATH="$<TARGET_FILE:${VMP_BINDINGS_CPP_PLUGIN_TARGET}>")
  else()
    target_compile_definitions(${target} PRIVATE VMP_CPP_PLUGIN_PATH="")
  endif()
endforeach()

target_link_libraries(vmp-protect PRIVATE vmp_policy vmp_runtime_audit)
