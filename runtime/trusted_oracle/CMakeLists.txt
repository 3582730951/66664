enable_language(ASM)

set(VMP_TRUSTED_ORACLE_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/src/oracle.cpp)

if((CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_SYSTEM_NAME STREQUAL "Android") AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64)$")
  list(APPEND VMP_TRUSTED_ORACLE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/syscall_linux_x64.asm.S)
elseif((CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_SYSTEM_NAME STREQUAL "Android") AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
  list(APPEND VMP_TRUSTED_ORACLE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/syscall_linux_arm64.asm.S)
elseif(APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
  list(APPEND VMP_TRUSTED_ORACLE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/syscall_ios_arm64.asm.S)
elseif(WIN32 AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|x86_64|amd64|ARM64|aarch64)$")
  list(APPEND VMP_TRUSTED_ORACLE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/syscall_windows_x64.cpp)
endif()

add_library(vmp_runtime_trusted_oracle STATIC ${VMP_TRUSTED_ORACLE_SOURCES})
target_link_libraries(vmp_runtime_trusted_oracle PUBLIC
  vmp_build_options
  vmp_runtime_audit
  vmp_runtime_strings)
target_include_directories(vmp_runtime_trusted_oracle PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include)
