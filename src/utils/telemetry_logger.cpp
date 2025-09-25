#include "utils/telemetry_logger.h"

#include <fstream>
#include <filesystem>

namespace puzzle71::telemetry {

void LogTelemetryLine(const TelemetryOptions& options, std::string_view payload_json) {
    // TODO(T034): Stream structured telemetry with size validation and alert handling.
    std::filesystem::create_directories(options.jsonl_dir);
    auto path = std::filesystem::path(options.jsonl_dir) / "puzzle71solver-placeholder.jsonl";
    std::ofstream ofs(path, std::ios::app);
    ofs << payload_json << '\n';
}

}  // namespace puzzle71::telemetry
