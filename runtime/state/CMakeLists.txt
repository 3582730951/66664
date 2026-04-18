add_library(vmp_runtime_state STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src/state.cpp)
target_link_libraries(vmp_runtime_state PUBLIC vmp_build_options vmp_policy vmp_runtime_audit)
target_include_directories(vmp_runtime_state PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
