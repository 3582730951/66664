#include <cstdlib>

#ifdef _WIN32
#include <vmp/loader/windows/windows_loader.h>
#else
#include <vmp/loader/linux/linux_loader.h>
#endif

int main() {
#ifdef _WIN32
  vmp_windows_loader_force_link();
#endif
  return EXIT_SUCCESS;
}
