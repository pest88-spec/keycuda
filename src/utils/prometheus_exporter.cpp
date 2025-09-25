#include "utils/prometheus_exporter.h"

#include <filesystem>
#include <fstream>

namespace puzzle71::telemetry {

void WritePrometheusSnapshot(const PrometheusOptions& options, std::string_view payload) {
    // TODO(T035): Emit Prometheus textfile metrics per spec with gauges for throughput + checkpoints.
    std::filesystem::create_directories(options.output_dir);
    auto path = std::filesystem::path(options.output_dir) / "puzzle71.prom";
    std::ofstream ofs(path, std::ios::trunc);
    ofs << payload;
}

}  // namespace puzzle71::telemetry
