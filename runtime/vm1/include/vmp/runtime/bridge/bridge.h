#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace vmp::runtime::vm1 {
class Vm1Module;
}
namespace vmp::runtime::vm2 {
class Vm2Module;
}

namespace vmp::runtime::bridge {

enum class Domain : std::uint8_t { native = 0, vm1 = 1, vm2 = 2 };

struct DomainCallArgs {
  std::vector<std::uint64_t> ints;
  std::vector<double> floats;
  std::vector<void*> opaque;
};

struct DomainCallResult {
  std::uint64_t ret_int = 0;
  double ret_float = 0.0;
  int status = 0;
};

class BridgeException : public std::runtime_error {
 public:
  explicit BridgeException(const std::string& message) : std::runtime_error(message) {}
};

class DomainCallException : public std::runtime_error {
 public:
  DomainCallException(int status_code, std::string message);

  int status_code() const noexcept { return status_code_; }

 private:
  int status_code_ = 0;
};

class BridgeRegistry {
 public:
  void register_native(std::uint32_t id, std::function<DomainCallResult(const DomainCallArgs&)> fn);
  void register_vm1(std::uint32_t id, vmp::runtime::vm1::Vm1Module* module);
  void register_vm2(std::uint32_t id, vmp::runtime::vm2::Vm2Module* module);
  DomainCallResult call(Domain target, std::uint32_t id, const DomainCallArgs& args, int max_depth = 64);

  std::shared_ptr<DomainCallException> last_domain_exception() const;
  void clear_last_domain_exception();

 private:
  std::unordered_map<std::uint32_t, std::function<DomainCallResult(const DomainCallArgs&)>> native_handlers_;
  std::unordered_map<std::uint32_t, vmp::runtime::vm1::Vm1Module*> vm1_handlers_;
  std::unordered_map<std::uint32_t, std::function<DomainCallResult(const DomainCallArgs&, BridgeRegistry*, int)>> vm2_handlers_;
};

}  // namespace vmp::runtime::bridge
