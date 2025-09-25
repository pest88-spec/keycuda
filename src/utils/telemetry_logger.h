#pragma once

#include <string>
#include <string_view>

namespace puzzle71::telemetry {

struct TelemetryOptions {
    std::string jsonl_dir{"telemetry"};
    std::string operator_id;
    std::string operator_purpose;
};

void LogTelemetryLine(const TelemetryOptions& options, std::string_view payload_json);

}  // namespace puzzle71::telemetry
