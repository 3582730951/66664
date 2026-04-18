#pragma once

#ifdef _WIN32
#include <windows.h>

extern "C" BOOL WINAPI vmp_windows_loader_dll_main(HINSTANCE instance, DWORD reason, LPVOID reserved);
extern "C" void vmp_windows_loader_force_link(void);
#endif

namespace vmp::loader::windows {

struct LoaderFacade {
  const char* status() const noexcept;
};

}  // namespace vmp::loader::windows
