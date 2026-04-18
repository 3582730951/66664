#pragma once

namespace vmp::loader::ios {

struct LoaderFacade {
  const char* status() const noexcept;
};

}  // namespace vmp::loader::ios

extern "C" void vmp_ios_init(void);
