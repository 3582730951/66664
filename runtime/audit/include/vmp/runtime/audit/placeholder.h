#pragma once

namespace vmp::runtime::audit {

void initialize_placeholder_hook_once() noexcept;

}  // namespace vmp::runtime::audit

extern "C" void vm_placeholder_analysis_awareness_hook(void);
