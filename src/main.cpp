#include "config/puzzle71_config.h"
#include "services/device_metrics.h"
#include "solver.h"
#include "utils/digest_verifier.h"
#include "utils/prometheus_exporter.h"
#include "utils/telemetry_logger.h"

#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace {

struct ParsedArgs {
    std::string keyspace_start;
    std::string keyspace_end;
    std::string target_address;
    std::string operator_id;
    std::string operator_purpose;
    bool dry_run{false};
    bool enable_checkpoint{false};
    std::optional<std::string> prometheus_dir;
};

void PrintUsage() {
    std::cerr << "Usage: Puzzle71Solver --keyspace <start:end> --target-address <addr> --operator-id <id> "
                 "--operator-purpose <purpose> [--dry-run] [--enable-checkpoint] "
                 "[--prometheus-export <dir>]" << std::endl;
}

ParsedArgs ParseArguments(int argc, char* argv[]) {
    ParsedArgs parsed;
    std::unordered_map<std::string, std::string> kv;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--dry-run") {
            parsed.dry_run = true;
            continue;
        }
        if (arg == "--enable-checkpoint") {
            parsed.enable_checkpoint = true;
            continue;
        }
        if (arg == "--prometheus-export") {
            if (i + 1 >= argc) {
                throw std::runtime_error("--prometheus-export requires a directory argument");
            }
            parsed.prometheus_dir = argv[++i];
            continue;
        }
        if (arg.starts_with("--")) {
            if (i + 1 >= argc) {
                throw std::runtime_error("Missing value for argument: " + arg);
            }
            kv[arg] = argv[++i];
        }
    }

    auto keyspace = kv.find("--keyspace");
    auto target = kv.find("--target-address");
    auto operator_id = kv.find("--operator-id");
    auto operator_purpose = kv.find("--operator-purpose");

    if (keyspace == kv.end() || target == kv.end() ||
        operator_id == kv.end() || operator_purpose == kv.end()) {
        throw std::runtime_error("Missing required arguments");
    }

    auto colon = keyspace->second.find(':');
    if (colon == std::string::npos) {
        throw std::runtime_error("--keyspace must be formatted as start:end");
    }

    parsed.keyspace_start = keyspace->second.substr(0, colon);
    parsed.keyspace_end = keyspace->second.substr(colon + 1);
    parsed.target_address = target->second;
    parsed.operator_id = operator_id->second;
    parsed.operator_purpose = operator_purpose->second;
    return parsed;
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        ParsedArgs parsed = ParseArguments(argc, argv);

        puzzle71::SolverOptions options{};
        options.keyspace_start_hex = parsed.keyspace_start;
        options.keyspace_end_hex = parsed.keyspace_end;
        options.target_address = parsed.target_address;
        options.operator_id = parsed.operator_id;
        options.operator_purpose = parsed.operator_purpose;
        options.enable_checkpoint = parsed.enable_checkpoint;

        if (auto cfg = puzzle71::config::LoadConfig("config/puzzle71.yaml")) {
            if (options.operator_id == "unset") options.operator_id = cfg->operator_meta.operator_id;
            if (options.operator_purpose == "development") options.operator_purpose = cfg->operator_meta.operator_purpose;
        }

        if (parsed.dry_run) {
            std::cout << "Dry run: configuration validated." << std::endl;
            return 0;
        }

        puzzle71::Puzzle71Solver solver(options);
        solver.Run();

        // TODO: integrate telemetry logging, Prometheus exporter, digest verification, and QA scripts (T042–T051).
        std::cout << "Puzzle71Solver run completed (stub)." << std::endl;
        return 0;

    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << std::endl;
        PrintUsage();
        return 1;
    }
}
