#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>

#include <vmp/runtime/audit/audit.h>

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: audit_cpp_format <record.json>\n";
    return 1;
  }
  try {
    std::ifstream input(argv[1]);
    if (!input) {
      throw std::runtime_error("failed to open input");
    }
    nlohmann::json data;
    input >> data;
    vmp::runtime::audit::AnalysisEventRecord record{
        data.at("event_date").get<std::string>(),        data.at("event_time").get<std::string>(),
        data.at("thread_id").get<std::uint64_t>(),       data.at("event_type").get<std::string>(),
        data.at("program_counter").get<std::uint64_t>(), data.at("module_name").get<std::string>(),
        data.at("symbol_name").get<std::string>(),       data.at("symbol_offset").get<std::int64_t>(),
        data.at("arch").get<std::string>(),              data.at("platform").get<std::string>(),
        data.at("process_id").get<std::uint64_t>(),      data.at("context_note").get<std::string>(),
    };
    std::cout << vmp::runtime::audit::format_line(record) << '\n';
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }
}
