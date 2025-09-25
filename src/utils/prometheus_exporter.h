#pragma once

#include <string>
#include <string_view>

namespace puzzle71::telemetry {

struct PrometheusOptions {
    std::string output_dir{"metrics"};
};

void WritePrometheusSnapshot(const PrometheusOptions& options, std::string_view payload);

}  // namespace puzzle71::telemetry
