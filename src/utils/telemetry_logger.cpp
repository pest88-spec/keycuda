#include "utils/telemetry_logger.h"

#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace {
constexpr std::size_t kMaxTelemetryLineLength = 1024;
constexpr const char* kTelemetryFilename = "puzzle71solver.ndjson";
}

namespace puzzle71::telemetry {

void LogTelemetryLine(const TelemetryOptions& options, std::string_view payload_json) {
    if (payload_json.size() > kMaxTelemetryLineLength) {
        throw std::runtime_error("Telemetry payload exceeds maximum length");
    }
    std::filesystem::create_directories(options.jsonl_dir);
    auto path = std::filesystem::path(options.jsonl_dir) / kTelemetryFilename;
    std::ofstream ofs(path, std::ios::app);
    ofs << payload_json << '\n';
}

}  // namespace puzzle71::telemetry
