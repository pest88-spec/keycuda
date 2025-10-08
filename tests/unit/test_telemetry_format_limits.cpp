#include <gtest/gtest.h>

#include "utils/telemetry_logger.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace {

std::string TempTelemetryDir() {
    auto base = std::filesystem::temp_directory_path();
    auto dir = base / std::filesystem::path("puzzle71_telemetry_" + std::to_string(std::rand()));
    std::filesystem::create_directories(dir);
    return dir.string();
}

}  // namespace

TEST(TelemetryFormatUnitTest, WritesTelemetryPayload) {
    auto dir = TempTelemetryDir();
    puzzle71::telemetry::TelemetryOptions options;
    options.jsonl_dir = dir;
    options.operator_id = "unit-test";
    options.operator_purpose = "verification";

    puzzle71::telemetry::LogTelemetryLine(options, R"({"keys":10})");

    auto telemetry_file = std::filesystem::path(dir) / "puzzle71solver.ndjson";
    ASSERT_TRUE(std::filesystem::exists(telemetry_file));
    std::ifstream ifs(telemetry_file);
    std::string line;
    std::getline(ifs, line);
    EXPECT_EQ(line, "{\"keys\":10}");

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST(TelemetryFormatUnitTest, RejectsOversizedLogLines) {
    auto dir = TempTelemetryDir();
    puzzle71::telemetry::TelemetryOptions options;
    options.jsonl_dir = dir;

    std::string oversized(2048, 'x');
    EXPECT_THROW(puzzle71::telemetry::LogTelemetryLine(options, oversized), std::runtime_error);

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}
