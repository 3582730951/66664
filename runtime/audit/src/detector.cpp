#include <vmp/runtime/audit/detector.h>

namespace vmp::runtime::audit {

std::string NullDetector::name() const { return "null"; }

void NullDetector::set_sink(std::function<void(const AnalysisEventRecord&)> sink) {
  std::lock_guard<std::mutex> lock(mutex_);
  sink_ = std::move(sink);
}

void NullDetector::start() noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  started_ = true;
}

void NullDetector::fire(const AnalysisEventRecord& record) noexcept {
  std::function<void(const AnalysisEventRecord&)> sink;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!started_ || !sink_) {
      return;
    }
    sink = sink_;
  }
  try {
    sink(record);
  } catch (...) {
  }
}

}  // namespace vmp::runtime::audit
