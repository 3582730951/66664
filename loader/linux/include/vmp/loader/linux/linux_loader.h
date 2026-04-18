#pragma once

#if defined(__cplusplus)
extern "C" {
#endif

void vmp_linux_init(void);

#if defined(__cplusplus)
}
#endif

namespace vmp::loader::linux_platform {

struct LoaderFacade {
  const char* status() const noexcept;
};

}  // namespace vmp::loader::linux_platform
