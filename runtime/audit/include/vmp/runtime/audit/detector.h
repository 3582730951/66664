#pragma once

#include <functional>
#include <mutex>
#include <string>

#include <vmp/runtime/audit/audit.h>

namespace vmp::runtime::audit {

class IDetector {
 public:
  virtual ~IDetector() = default;
  virtual std::string name() const = 0;
  virtual void set_sink(std::function<void(const AnalysisEventRecord&)>) = 0;
};

class NullDetector final : public IDetector {
 public:
  std::string name() const override;
  void set_sink(std::function<void(const AnalysisEventRecord&)>) override;

  void start() noexcept;
  void fire(const AnalysisEventRecord& record) noexcept;

 private:
  mutable std::mutex mutex_;
  std::function<void(const AnalysisEventRecord&)> sink_;
  bool started_ = false;
};

}  // namespace vmp::runtime::audit
