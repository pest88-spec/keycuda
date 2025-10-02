#include "config/puzzle71_config.h"
#include "services/device_metrics.h"
#include "solver.h"
#include "utils/digest_verifier.h"
#include "utils/prometheus_exporter.h"
#include "utils/telemetry_logger.h"

#include <filesystem>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <sstream>
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
    std::optional<std::string> telemetry_dir;
    std::optional<std::string> replay_manifest;
    std::optional<std::string> resume_manifest;
    std::string luck_file{"luck.txt"};
    std::optional<std::string> parity_test_scalar;
    std::optional<std::string> device_list;
};

std::vector<int> ParseDeviceList(const std::optional<std::string>& list) {
    std::vector<int> out;
    if (!list || list->empty()) {
        return out;
    }
    std::stringstream ss(*list);
    std::string token;
    while (std::getline(ss, token, ',')) {
        if (token.empty()) {
            continue;
        }
        try {
            int id = std::stoi(token);
            out.push_back(id);
        } catch (const std::exception&) {
            throw std::runtime_error("Invalid device id: " + token);
        }
    }
    return out;
}

void PrintUsage() {
    std::cerr << "Usage: Puzzle71Solver --keyspace <start:end> --target-address <addr> --operator-id <id> "
                 "--operator-purpose <purpose> [--device <ids>] [--dry-run] [--enable-checkpoint] "
                 "[--prometheus-export <dir>] [--telemetry-jsonl <dir>] [--replay-manifest <path>] "
                 "[--resume-manifest <path>] "
                 "[--luck-file <path>]" << std::endl;
}

ParsedArgs ParseArguments(int argc, char* argv[]) {
    ParsedArgs parsed;
    std::unordered_map<std::string, std::string> kv;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        struct ValueBinder {
            int& index;
            int argc;
            char** argv;
            void operator()(const std::string& flag, std::optional<std::string>& target) {
                if (index + 1 >= argc) {
                    throw std::runtime_error(flag + " requires a value");
                }
                target = argv[++index];
            }
            void operator()(const std::string& flag, std::string& target) {
                if (index + 1 >= argc) {
                    throw std::runtime_error(flag + " requires a value");
                }
                target = argv[++index];
            }
        } requires_value{i, argc, argv};
        if (arg == "--dry-run") {
            parsed.dry_run = true;
            continue;
        }
        if (arg == "--enable-checkpoint") {
            parsed.enable_checkpoint = true;
            continue;
        }
        if (arg == "--prometheus-export") {
            requires_value(arg, parsed.prometheus_dir);
            continue;
        }
        if (arg == "--telemetry-jsonl") {
            requires_value(arg, parsed.telemetry_dir);
            continue;
        }
        if (arg == "--replay-manifest") {
            requires_value(arg, parsed.replay_manifest);
            continue;
        }
        if (arg == "--resume-manifest") {
            requires_value(arg, parsed.resume_manifest);
            continue;
        }
        if (arg == "--parity-test-scalar") {
            requires_value(arg, parsed.parity_test_scalar);
            continue;
        }
        if (arg == "--device") {
            requires_value(arg, parsed.device_list);
            continue;
        }
        if (arg == "--luck-file") {
            requires_value(arg, parsed.luck_file);
            continue;
        }
        if (arg.size() >= 2 && arg[0] == '-' && arg[1] == '-') {
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

void RunPostAutomation(const std::filesystem::path& repo_root) {
    const std::filesystem::path qa_script = repo_root / "scripts/run-qa.sh";
    const std::filesystem::path report_script = repo_root / "scripts/generate-report.sh";

    if (std::filesystem::exists(qa_script)) {
        ::setenv("PUZZLE71_BENCHMARK_ARGS", "--dry-run-only --samples 1 --devices 0", 1);
        std::string command = qa_script.string() + " --mode smoke";
        int rc = std::system(command.c_str());
        if (rc != 0) {
            std::cerr << "run-qa.sh exited with code " << rc << std::endl;
        }
    }

    if (std::filesystem::exists(report_script)) {
        int rc = std::system(report_script.c_str());
        if (rc != 0) {
            std::cerr << "generate-report.sh exited with code " << rc << std::endl;
        }
    }
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
        options.dry_run = parsed.dry_run;
        options.telemetry_jsonl_dir = parsed.telemetry_dir;
        options.prometheus_dir = parsed.prometheus_dir;
        options.replay_manifest_path = parsed.replay_manifest;
        options.resume_manifest_path = parsed.resume_manifest;
        options.luck_file = parsed.luck_file;
        options.parity_test_scalar_hex = parsed.parity_test_scalar;
        options.device_ids = ParseDeviceList(parsed.device_list);

        if (options.parity_test_scalar_hex) {
            std::cerr << "[warning] Parity test mode enabled with scalar "
                      << *options.parity_test_scalar_hex << std::endl;
        }

        if (auto cfg = puzzle71::config::LoadConfig("config/puzzle71.yaml")) {
            options.replay_config = cfg->replay;
            if (options.operator_id.empty() || options.operator_id == "unset") {
                options.operator_id = cfg->operator_meta.operator_id;
            }
            if (options.operator_purpose.empty() || options.operator_purpose == "development") {
                options.operator_purpose = cfg->operator_meta.operator_purpose;
            }
            if (!options.telemetry_jsonl_dir && !cfg->replay.grid_dim.empty()) {
                options.telemetry_jsonl_dir = "telemetry";
            }
        }

        puzzle71::Puzzle71Solver solver(options);
        solver.Run();

        RunPostAutomation(std::filesystem::current_path());
        return 0;

    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << std::endl;
        PrintUsage();
        return 1;
    }
}
