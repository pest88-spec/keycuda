#include <gtest/gtest.h>

#include "utils/prometheus_exporter.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>

TEST(PrometheusExporterUnitTest, WritesMetricsWithGaugeNames) {
    auto tmp_dir = std::filesystem::temp_directory_path() /
                   std::filesystem::path("puzzle71_prom_" + std::to_string(std::rand()));
    std::filesystem::create_directories(tmp_dir);

    puzzle71::telemetry::PrometheusOptions options;
    options.output_dir = tmp_dir.string();

    const std::string payload = "metric_a 1\nmetric_b{label=\"value\"} 2\n";
    puzzle71::telemetry::WritePrometheusSnapshot(options, payload);

    auto prom_file = tmp_dir / "puzzle71.prom";
    ASSERT_TRUE(std::filesystem::exists(prom_file));

    std::ifstream ifs(prom_file);
    std::string contents((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    EXPECT_NE(contents.find("metric_a"), std::string::npos);
    EXPECT_NE(contents.find("metric_b"), std::string::npos);

    std::error_code ec;
    std::filesystem::remove_all(tmp_dir, ec);
}
