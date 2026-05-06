#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace vmp::runtime::trusted_oracle {

enum class ThreatLevel : std::uint8_t {
  low = 0,
  normal = 1,
  elevated = 2,
  high = 3,
};

struct VoteSource {
  std::string name;
  bool sampled = false;
  bool value = false;
  double confidence = 1.0;
  std::string detail;
};

struct KOfNResult {
  std::string fact_name;
  bool accepted = false;
  bool divergent = false;
  std::size_t threshold = 0;
  std::size_t sampled_count = 0;
  std::size_t positive_count = 0;
  std::size_t negative_count = 0;
  std::vector<std::string> positive_sources;
  std::vector<std::string> negative_sources;
  std::vector<std::string> missing_sources;
  std::string note;
};

class KOfNVoter {
 public:
  explicit KOfNVoter(ThreatLevel threat_level = ThreatLevel::normal,
                     std::size_t explicit_threshold = 0) noexcept
      : threat_level_(threat_level), explicit_threshold_(explicit_threshold) {}

  static std::size_t threshold_for(std::size_t sampled_count, ThreatLevel threat_level) noexcept {
    if (sampled_count == 0) {
      return 0;
    }
    if (sampled_count == 1) {
      return 1;
    }
    const auto majority = (sampled_count / 2u) + 1u;
    switch (threat_level) {
      case ThreatLevel::low:
        return majority;
      case ThreatLevel::normal:
        return std::max<std::size_t>(majority,
                                     static_cast<std::size_t>(std::ceil(static_cast<double>(sampled_count) * 2.0 / 3.0)));
      case ThreatLevel::elevated:
        return std::max<std::size_t>(majority, sampled_count - 1u);
      case ThreatLevel::high:
        return sampled_count;
    }
    return majority;
  }

  static double error_probability(std::size_t n, std::size_t k, double p) noexcept {
    if (n == 0 || k == 0 || k > n || p < 0.0 || p > 1.0) {
      return 0.0;
    }
    auto binom = [](std::size_t nn, std::size_t rr) noexcept -> double {
      if (rr > nn) {
        return 0.0;
      }
      rr = std::min(rr, nn - rr);
      double out = 1.0;
      for (std::size_t i = 1; i <= rr; ++i) {
        out *= static_cast<double>(nn - rr + i);
        out /= static_cast<double>(i);
      }
      return out;
    };

    double tail = 0.0;
    for (std::size_t i = k; i <= n; ++i) {
      tail += binom(n, i) * std::pow(p, static_cast<double>(i)) *
              std::pow(1.0 - p, static_cast<double>(n - i));
    }
    return tail;
  }

  KOfNResult evaluate(std::string fact_name, const std::vector<VoteSource>& sources) const {
    KOfNResult result;
    result.fact_name = std::move(fact_name);

    for (const auto& source : sources) {
      if (!source.sampled) {
        result.missing_sources.push_back(source.name);
        continue;
      }
      ++result.sampled_count;
      if (source.value) {
        ++result.positive_count;
        result.positive_sources.push_back(source.name);
      } else {
        ++result.negative_count;
        result.negative_sources.push_back(source.name);
      }
    }

    result.threshold = explicit_threshold_ == 0 ? threshold_for(result.sampled_count, threat_level_)
                                                : std::min(explicit_threshold_, result.sampled_count);
    result.accepted = result.sampled_count != 0 && result.positive_count >= result.threshold;
    result.divergent = result.positive_count > 0 && result.negative_count > 0;
    result.note = make_note(result, sources);
    return result;
  }

 private:
  static void append_joined(std::ostringstream& oss, const char* label, const std::vector<std::string>& values) {
    oss << ' ' << label << '=';
    if (values.empty()) {
      oss << "[]";
      return;
    }
    for (std::size_t i = 0; i < values.size(); ++i) {
      if (i != 0) {
        oss << ',';
      }
      oss << values[i];
    }
  }

  static std::string make_note(const KOfNResult& result, const std::vector<VoteSource>& sources) {
    std::ostringstream oss;
    oss << "fact=" << result.fact_name
        << " accepted=" << (result.accepted ? "true" : "false")
        << " divergent=" << (result.divergent ? "true" : "false")
        << " k=" << result.threshold
        << " n=" << result.sampled_count
        << " positive=" << result.positive_count
        << " negative=" << result.negative_count;
    append_joined(oss, "positive_sources", result.positive_sources);
    append_joined(oss, "negative_sources", result.negative_sources);
    append_joined(oss, "missing_sources", result.missing_sources);
    for (const auto& source : sources) {
      if (!source.detail.empty()) {
        oss << " detail." << source.name << '=' << source.detail;
      }
    }
    return oss.str();
  }

  ThreatLevel threat_level_ = ThreatLevel::normal;
  std::size_t explicit_threshold_ = 0;
};

}  // namespace vmp::runtime::trusted_oracle
