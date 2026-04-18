add_library(vmp_loader_common STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src/platform_caps.cpp)
target_link_libraries(vmp_loader_common PUBLIC vmp_build_options)
target_include_directories(vmp_loader_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
