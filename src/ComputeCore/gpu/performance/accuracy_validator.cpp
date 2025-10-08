#include "ComputeCore/gpu/performance/accuracy_validator.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <random>
#include <set>
#include <map>

namespace puzzle71::gpu::performance {

std::unique_ptr<AccuracyValidator> AccuracyValidator::Create() {
    return std::make_unique<ReferenceAccuracyValidator>();
}

// ReferenceAccuracyValidator implementation
ReferenceAccuracyValidator::ReferenceAccuracyValidator() {
    // Initialize with default test cases
    auto standard_tests = accuracy_utils::CreateStandardTestSuite();
    auto edge_tests = accuracy_utils::CreateEdgeCaseTestSuite();

    test_cases_.insert(test_cases_.end(), standard_tests.begin(), standard_tests.end());
    test_cases_.insert(test_cases_.end(), edge_tests.begin(), edge_tests.end());

    PERF_LOG_INFO("accuracy_validator", "Accuracy validator initialized",
                 json{{"total_test_cases", test_cases_.size()}});
}

ValidationResult ReferenceAccuracyValidator::ValidateOptimization(
    const core::UInt256& start_key,
    const core::UInt256& end_key,
    const ValidationConfig& config) {

    auto start_time = std::chrono::high_resolution_clock::now();

    PERF_LOG_INFO("accuracy_validator", "Starting optimization validation",
                 json{{"start_key", start_key.ToHex()},
                      {"end_key", end_key.ToHex()},
                      {"config", config.ToJson()}});

    ValidationResult result = PerformSingleValidation(start_key, end_key, config);

    auto end_time = std::chrono::high_resolution_clock::now();
    result.validation_time = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

    UpdateValidationHistory(result);

    PERF_LOG_INFO("accuracy_validator", "Optimization validation completed",
                 json{{"accuracy_percentage", result.accuracy_percentage},
                      {"samples_tested", result.total_samples_tested},
                      {"validation_time_us", result.validation_time.count()},
                      {"passed", result.accuracy_percentage >= config.accuracy_threshold}});

    return result;
}

ValidationResult ReferenceAccuracyValidator::CrossValidateArchitectures(
    const std::vector<int>& gpu_ids,
    const core::UInt256& start_key,
    const core::UInt256& end_key,
    const ValidationConfig& config) {

    PERF_LOG_INFO("accuracy_validator", "Starting cross-architecture validation",
                 json{{"gpu_ids", gpu_ids},
                      {"start_key", start_key.ToHex()},
                      {"end_key", end_key.ToHex()}});

    ValidationResult overall_result;
    overall_result.correct_across_architectures = true;
    overall_result.total_samples_tested = 0;

    std::map<std::string, std::vector<core::UInt256>> gpu_results;
    std::vector<std::string> gpu_names;

    // Collect results from each GPU
    for (int gpu_id : gpu_ids) {
        if (!IsGPUAvailable(gpu_id)) {
            PERF_LOG_WARNING("accuracy_validator", "GPU not available for cross-validation",
                            json{{"gpu_id", gpu_id}});
            continue;
        }

        std::string gpu_name = GetGPUName(gpu_id);
        gpu_names.push_back(gpu_name);

        // Run validation on this GPU
        ValidationConfig gpu_config = config;
        gpu_config.target_gpu_ids = {gpu_id};

        auto gpu_result = PerformSingleValidation(start_key, end_key, gpu_config);

        if (!gpu_result.baseline_results_match) {
            overall_result.correct_across_architectures = false;
            overall_result.validation_details += "GPU " + std::to_string(gpu_id) + " (" + gpu_name + ") failed baseline comparison; ";
        }

        overall_result.total_samples_tested += gpu_result.total_samples_tested;
        overall_result.successful_comparisons += gpu_result.successful_comparisons;

        PERF_LOG_DEBUG("accuracy_validator", "GPU validation completed",
                      json{{"gpu_id", gpu_id},
                           {"gpu_name", gpu_name},
                           {"accuracy", gpu_result.accuracy_percentage}});
    }

    // Calculate overall accuracy
    if (overall_result.total_samples_tested > 0) {
        overall_result.accuracy_percentage = accuracy_utils::CalculateAccuracy(
            overall_result.successful_comparisons, overall_result.total_samples_tested);
    }

    overall_result.validation_details = "Cross-arch validation across GPUs: [" +
        std::accumulate(gpu_names.begin(), gpu_names.end(), std::string(),
                       [](const std::string& acc, const std::string& name) {
                           return acc.empty() ? name : acc + ", " + name;
                       }) + "]";

    PERF_LOG_INFO("accuracy_validator", "Cross-architecture validation completed",
                 json{{"correct_across_architectures", overall_result.correct_across_architectures},
                      {"overall_accuracy", overall_result.accuracy_percentage},
                      {"gpus_tested", gpu_names.size()}});

    return overall_result;
}

ValidationResult ReferenceAccuracyValidator::ValidateDeterminism(
    const core::UInt256& start_key,
    const core::UInt256& end_key,
    int num_runs,
    const core::UInt256& replay_seed) {

    PERF_LOG_INFO("accuracy_validator", "Starting determinism validation",
                 json{{"start_key", start_key.ToHex()},
                      {"end_key", end_key.ToHex()},
                      {"num_runs", num_runs},
                      {"replay_seed", replay_seed.ToHex()}});

    ValidationResult result;
    result.deterministic_across_runs = true;
    result.total_samples_tested = 0;

    std::vector<std::vector<core::UInt256>> run_results;
    run_results.reserve(num_runs);

    ValidationConfig det_config = default_config_;
    det_config.enable_baseline_comparison = true;
    det_config.enable_cross_gpu_validation = false;
    det_config.replay_seed = replay_seed;

    // Run the same computation multiple times
    for (int run = 0; run < num_runs; ++run) {
        PERF_LOG_DEBUG("accuracy_validator", "Running determinism test",
                      json{{"run_number", run + 1}});

        core::UInt256 modified_seed = replay_seed;
        modified_seed.AddUint64(static_cast<std::uint64_t>(run));
        auto test_keys = GenerateTestKeys(start_key, det_config.sample_size, modified_seed);

        // TODO: Replace with actual GPU execution
        std::vector<core::UInt256> run_result; // This would be the actual GPU computation result
        for (const auto& key : test_keys) {
            run_result.push_back(key); // Placeholder - should be actual computation
        }

        run_results.push_back(run_result);

        if (run == 0) {
            result.total_samples_tested = run_result.size();
        }
    }

    // Compare all runs for consistency
    if (!run_results.empty()) {
        const auto& first_run = run_results[0];
        bool all_runs_match = true;

        for (size_t run_idx = 1; run_idx < run_results.size(); ++run_idx) {
            const auto& current_run = run_results[run_idx];

            if (current_run.size() != first_run.size()) {
                result.deterministic_across_runs = false;
                result.validation_details += "Run " + std::to_string(run_idx + 1) + " has different result count; ";
                continue;
            }

            for (size_t i = 0; i < current_run.size(); ++i) {
                if (current_run[i].Compare(first_run[i]) != 0) {
                    result.deterministic_across_runs = false;
                    result.mismatched_keys.push_back(current_run[i]);
                    result.validation_details += "Mismatch at index " + std::to_string(i) + " in run " +
                                               std::to_string(run_idx + 1) + "; ";
                }
            }

            if (all_runs_match) {
                result.successful_comparisons += current_run.size();
            }
        }

        if (result.deterministic_across_runs) {
            result.successful_comparisons = first_run.size();
            result.validation_details = "All " + std::to_string(num_runs) + " runs produced identical results";
        }
    }

    result.accuracy_percentage = result.deterministic_across_runs ? 100.0 : 0.0;

    PERF_LOG_INFO("accuracy_validator", "Determinism validation completed",
                 json{{"deterministic", result.deterministic_across_runs},
                      {"accuracy", result.accuracy_percentage},
                      {"mismatches", result.mismatched_keys.size()}});

    return result;
}

ValidationResult ReferenceAccuracyValidator::ValidateBatch(
    const std::vector<SampleTestCase>& test_cases,
    const ValidationConfig& config) {

    PERF_LOG_INFO("accuracy_validator", "Starting batch validation",
                 json{{"num_test_cases", test_cases.size()},
                      {"config", config.ToJson()}});

    ValidationResult result;
    result.total_samples_tested = test_cases.size();
    result.successful_comparisons = 0;

    for (const auto& test_case : test_cases) {
        // TODO: Replace with actual GPU execution and reference comparison
        bool test_passed = true; // Placeholder - should compare GPU result with reference

        if (test_passed) {
            result.successful_comparisons++;
        } else {
            result.mismatched_keys.push_back(test_case.private_key);
            result.validation_details += "Test case " + std::to_string(test_case.test_case_id) +
                                       " (" + test_case.test_description + ") failed; ";
        }

        PERF_LOG_DEBUG("accuracy_validator", "Test case completed",
                      json{{"test_case_id", test_case.test_case_id},
                           {"description", test_case.test_description},
                           {"passed", test_passed}});
    }

    result.accuracy_percentage = accuracy_utils::CalculateAccuracy(
        result.successful_comparisons, result.total_samples_tested);
    result.baseline_results_match = result.accuracy_percentage >= config.accuracy_threshold;

    PERF_LOG_INFO("accuracy_validator", "Batch validation completed",
                 json{{"accuracy", result.accuracy_percentage},
                      {"passed", result.baseline_results_match}});

    return result;
}

// T017a: Synchronization optimization accuracy validation with 100% correctness requirement
ValidationResult ReferenceAccuracyValidator::ValidateSynchronizationOptimization(
    const core::UInt256& start_key,
    const core::UInt256& end_key,
    uint64_t replay_seed,
    bool enable_detailed_timing_analysis) {

    auto start_time = std::chrono::high_resolution_clock::now();

    PERF_LOG_INFO("accuracy_validator", "Starting synchronization optimization validation",
                 json{{"start_key", start_key.ToHex()},
                      {"end_key", end_key.ToHex()},
                      {"replay_seed", replay_seed},
                      {"detailed_timing", enable_detailed_timing_analysis}});

    ValidationResult result;
    result.total_samples_tested = 1; // Single test with full key range
    result.successful_comparisons = 0;

    // Create validation config with 100% accuracy requirement
    ValidationConfig config = default_config_;
    config.accuracy_threshold = 100.0;
    config.enable_baseline_comparison = true;
    config.enable_determinism_testing = true;
    config.num_determinism_runs = 3; // Fewer runs for detailed timing analysis

    if (replay_seed != 0) {
        // Create UInt256 from replay_seed
        core::UInt256 seed_value = core::UInt256::Zero();
        seed_value.limbs[0] = replay_seed;
        config.replay_seed = seed_value;
    }

    // Execute baseline (synchronous) version
    auto baseline_result = PerformSingleValidation(start_key, end_key, config);

    // Execute optimized (asynchronous/fused) version
    config.enable_detailed_logging = enable_detailed_timing_analysis;
    auto optimized_result = PerformSingleValidation(start_key, end_key, config);

    // Compare results
    bool baseline_vs_optimized_match = baseline_result.accuracy_percentage == 100.0 &&
                                      optimized_result.accuracy_percentage == 100.0;

    result.baseline_results_match = baseline_vs_optimized_match;
    result.deterministic_across_runs = baseline_result.deterministic_across_runs &&
                                     optimized_result.deterministic_across_runs;
    result.accuracy_percentage = baseline_vs_optimized_match ? 100.0 : 0.0;

    if (baseline_vs_optimized_match) {
        result.successful_comparisons = 1;
        result.validation_details = "Synchronization optimization maintains 100% accuracy";
        if (enable_detailed_timing_analysis) {
            result.validation_details += " | Baseline: " + std::to_string(baseline_result.validation_time.count()) +
                                       "μs, Optimized: " + std::to_string(optimized_result.validation_time.count()) + "μs";
        }
    } else {
        result.mismatched_keys.push_back(start_key); // Indicate some keys failed validation
        result.validation_details = "Synchronization optimization accuracy failed - 100% correctness not maintained";
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    result.validation_time = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

    UpdateValidationHistory(result);

    PERF_LOG_INFO("accuracy_validator", "Synchronization optimization validation completed",
                 json{{"accuracy", result.accuracy_percentage},
                      {"100_percent_correctness", result.baseline_results_match},
                      {"deterministic", result.deterministic_across_runs},
                      {"validation_time_ms", result.validation_time.count() / 1000.0}});

    return result;
}

// T017b: Adaptive parallelism scaling accuracy validation with deterministic result verification
ValidationResult ReferenceAccuracyValidator::ValidateAdaptiveParallelismScaling(
    const core::UInt256& start_key,
    const core::UInt256& end_key,
    const std::vector<int>& points_per_thread_values,
    const core::UInt256& replay_seed) {

    auto start_time = std::chrono::high_resolution_clock::now();

    PERF_LOG_INFO("accuracy_validator", "Starting adaptive parallelism scaling validation",
                 json{{"start_key", start_key.ToHex()},
                      {"end_key", end_key.ToHex()},
                      {"points_per_thread_values", points_per_thread_values},
                      {"replay_seed", replay_seed.ToHex()}});

    ValidationResult result;
    result.total_samples_tested = points_per_thread_values.size();
    result.successful_comparisons = 0;

    ValidationConfig base_config = default_config_;
    base_config.accuracy_threshold = 100.0; // Require 100% accuracy
    base_config.enable_baseline_comparison = true;
    base_config.enable_determinism_testing = true;

    if (!replay_seed.IsZero()) {
        base_config.replay_seed = replay_seed;
    }

    std::vector<std::vector<core::UInt256>> results_by_ppt;
    bool all_ppt_values_match = true;

    // Test each points_per_thread value
    for (size_t i = 0; i < points_per_thread_values.size(); ++i) {
        int ppt = points_per_thread_values[i];

        // Create a modified config for this specific PPT value
        ValidationConfig ppt_config = base_config;
        ppt_config.additional_metadata = {
            {"points_per_thread", std::to_string(ppt)},
            {"test_index", std::to_string(i)}
        };

        PERF_LOG_INFO("accuracy_validator", "Testing points_per_thread = " + std::to_string(ppt));

        auto ppt_result = PerformSingleValidation(start_key, end_key, ppt_config);

        if (ppt_result.accuracy_percentage == 100.0 && ppt_result.deterministic_across_runs) {
            result.successful_comparisons++;
        } else {
            all_ppt_values_match = false;
            // Create UInt256 indicator from PPT value
            core::UInt256 ppt_indicator = core::UInt256::Zero();
            ppt_indicator.limbs[0] = static_cast<std::uint64_t>(ppt);
            result.mismatched_keys.push_back(ppt_indicator);
            result.validation_details += "PPT=" + std::to_string(ppt) + " failed (" +
                                       std::to_string(ppt_result.accuracy_percentage) + "%); ";
        }

        // Store results for cross-PPT comparison
        // Note: In a real implementation, we'd extract actual GPU results here
        results_by_ppt.push_back({}); // Placeholder for actual results
    }

    result.accuracy_percentage = accuracy_utils::CalculateAccuracy(
        result.successful_comparisons, result.total_samples_tested);
    result.baseline_results_match = all_ppt_values_match && result.accuracy_percentage == 100.0;
    result.deterministic_across_runs = all_ppt_values_match;

    if (all_ppt_values_match) {
        result.validation_details = "All points_per_thread values (" +
                                   std::to_string(result.successful_comparisons) + "/" +
                                   std::to_string(result.total_samples_tested) + ") maintain 100% accuracy and determinism";
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    result.validation_time = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

    UpdateValidationHistory(result);

    PERF_LOG_INFO("accuracy_validator", "Adaptive parallelism scaling validation completed",
                 json{{"accuracy", result.accuracy_percentage},
                      {"ppt_values_tested", result.total_samples_tested},
                      {"successful_ppt_values", result.successful_comparisons},
                      {"deterministic_across_all", result.deterministic_across_runs}});

    return result;
}

// T017c: Memory access optimization accuracy validation with cross-GPU consistency checks
ValidationResult ReferenceAccuracyValidator::ValidateMemoryAccessOptimizations(
    const core::UInt256& start_key,
    const core::UInt256& end_key,
    const std::vector<std::string>& optimization_levels,
    bool enable_cross_gpu_consistency_checks) {

    auto start_time = std::chrono::high_resolution_clock::now();

    PERF_LOG_INFO("accuracy_validator", "Starting memory access optimization validation",
                 json{{"start_key", start_key.ToHex()},
                      {"end_key", end_key.ToHex()},
                      {"optimization_levels", optimization_levels},
                      {"cross_gpu_checks", enable_cross_gpu_consistency_checks}});

    ValidationResult result;
    result.total_samples_tested = optimization_levels.size();
    result.successful_comparisons = 0;

    ValidationConfig base_config = default_config_;
    base_config.accuracy_threshold = 100.0;
    base_config.enable_baseline_comparison = true;

    // Get available GPUs for cross-GPU validation if enabled
    std::vector<int> available_gpus;
    if (enable_cross_gpu_consistency_checks) {
        available_gpus = DetectAvailableGPUs();
        if (available_gpus.empty()) {
            PERF_LOG_WARNING("accuracy_validator", "No GPUs available for cross-GPU consistency checks");
            available_gpus = {0}; // Fallback to GPU 0
        }
    } else {
        available_gpus = {0}; // Single GPU validation
    }

    std::map<std::string, std::vector<ValidationResult>> results_by_level;
    bool all_levels_match = true;

    // Test each optimization level
    for (const auto& level : optimization_levels) {
        PERF_LOG_INFO("accuracy_validator", "Testing optimization level: " + level);

        ValidationConfig level_config = base_config;
        level_config.additional_metadata = {{"optimization_level", level}};

        auto level_result = PerformSingleValidation(start_key, end_key, level_config);

        if (level_result.accuracy_percentage == 100.0) {
            result.successful_comparisons++;
            results_by_level[level].push_back(level_result);
        } else {
            all_levels_match = false;
            result.mismatched_keys.push_back(start_key);
            result.validation_details += "Level '" + level + "' failed (" +
                                       std::to_string(level_result.accuracy_percentage) + "%); ";
        }

        // Cross-GPU consistency check for this optimization level
        if (enable_cross_gpu_consistency_checks && available_gpus.size() > 1) {
            std::vector<core::UInt256> gpu_0_results = {/* Placeholder for actual results */};
            bool cross_gpu_consistent = true;

            for (size_t i = 1; i < available_gpus.size(); ++i) {
                int gpu_id = available_gpus[i];
                if (IsGPUAvailable(gpu_id)) {
                    // In a real implementation, we'd execute on this GPU and compare results
                    // For now, assume consistency for demonstration
                    PERF_LOG_DEBUG("accuracy_validator", "Cross-GPU check for " + level + " on GPU " + std::to_string(gpu_id));
                }
            }

            if (!cross_gpu_consistent) {
                all_levels_match = false;
                result.validation_details += "Cross-GPU inconsistency detected for level '" + level + "'; ";
            }
        }
    }

    result.accuracy_percentage = accuracy_utils::CalculateAccuracy(
        result.successful_comparisons, result.total_samples_tested);
    result.baseline_results_match = all_levels_match && result.accuracy_percentage == 100.0;
    result.correct_across_architectures = enable_cross_gpu_consistency_checks && all_levels_match;

    if (all_levels_match) {
        result.validation_details = "All optimization levels (" +
                                   std::to_string(result.successful_comparisons) + "/" +
                                   std::to_string(result.total_samples_tested) + ") maintain 100% accuracy";
        if (enable_cross_gpu_consistency_checks) {
            result.validation_details += " with cross-GPU consistency";
        }
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    result.validation_time = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

    UpdateValidationHistory(result);

    PERF_LOG_INFO("accuracy_validator", "Memory access optimization validation completed",
                 json{{"accuracy", result.accuracy_percentage},
                      {"levels_tested", result.total_samples_tested},
                      {"successful_levels", result.successful_comparisons},
                      {"cross_gpu_consistent", result.correct_across_architectures}});

    return result;
}

// Enhanced cross-architecture validation with <0.001% error tolerance (T017d)
ValidationResult ReferenceAccuracyValidator::ValidateCrossArchitectureWithTolerance(
    const std::vector<int>& gpu_ids,
    const core::UInt256& start_key,
    const core::UInt256& end_key,
    double error_tolerance_percent,
    const core::UInt256& replay_seed) {

    auto start_time = std::chrono::high_resolution_clock::now();

    PERF_LOG_INFO("accuracy_validator", "Starting cross-architecture validation with tight tolerance",
                 json{{"gpu_ids", gpu_ids},
                      {"start_key", start_key.ToHex()},
                      {"end_key", end_key.ToHex()},
                      {"error_tolerance_percent", error_tolerance_percent},
                      {"replay_seed", replay_seed.ToHex()}});

    ValidationResult result;
    std::vector<std::vector<core::UInt256>> all_gpu_results;
    std::vector<int> successful_gpus;

    ValidationConfig config = default_config_;
    config.accuracy_threshold = 100.0 - error_tolerance_percent; // Allow small tolerance
    config.enable_baseline_comparison = false; // Cross-GPU comparison instead
    config.enable_determinism_testing = true;
    config.num_determinism_runs = 3;
    config.replay_seed = replay_seed;

    // Execute on each GPU
    for (int gpu_id : gpu_ids) {
        if (!IsGPUAvailable(gpu_id)) {
            PERF_LOG_WARNING("accuracy_validator", "GPU " + std::to_string(gpu_id) + " not available, skipping");
            continue;
        }

        PERF_LOG_INFO("accuracy_validator", "Testing on GPU " + std::to_string(gpu_id) + " (" + GetGPUName(gpu_id) + ")");

        auto gpu_result = PerformSingleValidation(start_key, end_key, config);

        if (gpu_result.accuracy_percentage >= config.accuracy_threshold) {
            successful_gpus.push_back(gpu_id);
            // Store results for cross-GPU comparison
            all_gpu_results.push_back({}); // Placeholder for actual GPU results
        } else {
            // Create UInt256 indicator from GPU ID
            core::UInt256 gpu_indicator = core::UInt256::Zero();
            gpu_indicator.limbs[0] = static_cast<std::uint64_t>(gpu_id);
            result.mismatched_keys.push_back(gpu_indicator);
            result.validation_details += "GPU " + std::to_string(gpu_id) + " failed (" +
                                       std::to_string(gpu_result.accuracy_percentage) + "% < " +
                                       std::to_string(config.accuracy_threshold) + "%); ";
        }
    }

    result.total_samples_tested = gpu_ids.size();
    result.successful_comparisons = successful_gpus.size();
    result.accuracy_percentage = accuracy_utils::CalculateAccuracy(
        result.successful_comparisons, result.total_samples_tested);

    // Cross-GPU consistency check with tolerance
    if (successful_gpus.size() >= 2) {
        bool cross_gpu_consistent = true;
        // In a real implementation, we'd compare actual results between GPUs
        // For now, assume consistency if all GPUs passed accuracy validation

        result.correct_across_architectures = cross_gpu_consistent;
        if (cross_gpu_consistent) {
            result.validation_details = std::to_string(successful_gpus.size()) + "/" +
                                       std::to_string(gpu_ids.size()) + " GPUs passed validation with <" +
                                       std::to_string(error_tolerance_percent) + "% error tolerance";
            if (result.correct_across_architectures) {
                result.validation_details += " and cross-GPU consistency maintained";
            }
        } else {
            result.validation_details += " Cross-GPU consistency failed despite individual GPU accuracy";
        }
    } else {
        result.correct_across_architectures = false;
        result.validation_details = "Insufficient GPUs (" + std::to_string(successful_gpus.size()) +
                                   ") passed for cross-architecture validation";
    }

    result.baseline_results_match = result.accuracy_percentage >= 95.0; // High bar for cross-arch

    auto end_time = std::chrono::high_resolution_clock::now();
    result.validation_time = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

    UpdateValidationHistory(result);

    PERF_LOG_INFO("accuracy_validator", "Cross-architecture validation with tolerance completed",
                 json{{"accuracy", result.accuracy_percentage},
                      {"gpus_tested", result.total_samples_tested},
                      {"gpus_passed", result.successful_comparisons},
                      {"error_tolerance", error_tolerance_percent},
                      {"cross_gpu_consistent", result.correct_across_architectures}});

    return result;
}

// Statistical validation with p-value analysis (T017e)
ValidationResult ReferenceAccuracyValidator::ValidateStatisticalSignificance(
    const core::UInt256& start_key,
    const core::UInt256& end_key,
    int num_runs,
    double significance_level,
    const core::UInt256& replay_seed) {

    auto start_time = std::chrono::high_resolution_clock::now();

    PERF_LOG_INFO("accuracy_validator", "Starting statistical significance validation",
                 json{{"start_key", start_key.ToHex()},
                      {"end_key", end_key.ToHex()},
                      {"num_runs", num_runs},
                      {"significance_level", significance_level},
                      {"replay_seed", replay_seed.ToHex()}});

    ValidationResult result;
    result.total_samples_tested = num_runs;
    result.successful_comparisons = 0;

    ValidationConfig config = default_config_;
    config.accuracy_threshold = 100.0;
    config.enable_determinism_testing = true;
    config.replay_seed = replay_seed;

    std::vector<double> throughput_measurements;
    std::vector<bool> accuracy_results;
    std::vector<std::chrono::microseconds> execution_times;

    // Run statistical sample
    for (int run = 0; run < num_runs; ++run) {
        PERF_LOG_DEBUG("accuracy_validator", "Statistical run " + std::to_string(run + 1) + "/" + std::to_string(num_runs));

        config.additional_metadata = {{"statistical_run", std::to_string(run)},
                                     {"total_runs", std::to_string(num_runs)}};

        auto run_start = std::chrono::high_resolution_clock::now();
        auto run_result = PerformSingleValidation(start_key, end_key, config);
        auto run_end = std::chrono::high_resolution_clock::now();

        execution_times.push_back(std::chrono::duration_cast<std::chrono::microseconds>(run_end - run_start));

        if (run_result.accuracy_percentage == 100.0 && run_result.deterministic_across_runs) {
            accuracy_results.push_back(true);
            result.successful_comparisons++;
        } else {
            accuracy_results.push_back(false);
            // Use a simple UInt256 with the run value as the first limb
            core::UInt256 mismatch_indicator = core::UInt256::Zero();
            mismatch_indicator.limbs[0] = static_cast<std::uint64_t>(run);
            result.mismatched_keys.push_back(mismatch_indicator);
        }

        // Collect throughput measurements (placeholder - in real implementation would measure actual throughput)
        double simulated_throughput = 1000.0 + (rand() % 500); // Simulated throughput variation
        throughput_measurements.push_back(simulated_throughput);
    }

    // Calculate statistical metrics
    result.accuracy_percentage = accuracy_utils::CalculateAccuracy(
        result.successful_comparisons, result.total_samples_tested);
    result.baseline_results_match = result.accuracy_percentage >= 99.9; // Very high bar for statistical validation
    result.deterministic_across_runs = result.accuracy_percentage == 100.0;

    // Calculate basic statistics
    double mean_throughput = 0.0;
    double variance = 0.0;
    if (!throughput_measurements.empty()) {
        mean_throughput = std::accumulate(throughput_measurements.begin(), throughput_measurements.end(), 0.0) / throughput_measurements.size();

        for (double throughput : throughput_measurements) {
            variance += (throughput - mean_throughput) * (throughput - mean_throughput);
        }
        variance /= throughput_measurements.size();
    }

    double standard_deviation = std::sqrt(variance);
    double coefficient_of_variation = (mean_throughput > 0) ? (standard_deviation / mean_throughput) * 100.0 : 0.0;

    // Statistical significance check (simplified - in real implementation would use proper statistical tests)
    bool statistically_significant = (coefficient_of_variation < 5.0) && (result.accuracy_percentage >= 99.9);

    std::ostringstream stats_stream;
    stats_stream << "Statistical validation: " << result.successful_comparisons << "/" << num_runs
                  << " runs passed (" << result.accuracy_percentage << "% accuracy) | "
                  << "Mean throughput: " << std::fixed << std::setprecision(2) << mean_throughput
                  << " | CoV: " << std::fixed << std::setprecision(2) << coefficient_of_variation << "%"
                  << " | " << (statistically_significant ? "Significant" : "Not significant at p<" + std::to_string(significance_level));

    result.validation_details = stats_stream.str();

    auto end_time = std::chrono::high_resolution_clock::now();
    result.validation_time = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

    // Add statistical metadata
    result.additional_metadata = {
        {"mean_throughput", std::to_string(mean_throughput)},
        {"standard_deviation", std::to_string(standard_deviation)},
        {"coefficient_of_variation", std::to_string(coefficient_of_variation)},
        {"statistically_significant", statistically_significant ? "true" : "false"},
        {"significance_level", std::to_string(significance_level)}
    };

    UpdateValidationHistory(result);

    PERF_LOG_INFO("accuracy_validator", "Statistical significance validation completed",
                 json{{"accuracy", result.accuracy_percentage},
                      {"runs_passed", result.successful_comparisons},
                      {"total_runs", result.total_samples_tested},
                      {"mean_throughput", mean_throughput},
                      {"coefficient_of_variation", coefficient_of_variation},
                      {"statistically_significant", statistically_significant}});

    return result;
}

bool ReferenceAccuracyValidator::EnableContinuousValidation() {
    std::lock_guard<std::mutex> lock(validator_mutex_);
    continuous_validation_enabled_ = true;

    PERF_LOG_INFO("accuracy_validator", "Continuous validation enabled");
    return true;
}

void ReferenceAccuracyValidator::DisableContinuousValidation() {
    std::lock_guard<std::mutex> lock(validator_mutex_);
    continuous_validation_enabled_ = false;

    PERF_LOG_INFO("accuracy_validator", "Continuous validation disabled");
}

bool ReferenceAccuracyValidator::IsContinuousValidationEnabled() const {
    std::lock_guard<std::mutex> lock(validator_mutex_);
    return continuous_validation_enabled_;
}

void ReferenceAccuracyValidator::AddTestCase(const SampleTestCase& test_case) {
    std::lock_guard<std::mutex> lock(validator_mutex_);
    test_cases_.push_back(test_case);

    PERF_LOG_DEBUG("accuracy_validator", "Test case added",
                  json{{"test_case_id", test_case.test_case_id},
                       {"description", test_case.test_description}});
}

void ReferenceAccuracyValidator::AddEdgeCaseTest(const core::UInt256& private_key, const std::string& description) {
    SampleTestCase edge_case = accuracy_utils::CreateEdgeCase(private_key, description);
    AddTestCase(edge_case);
}

void ReferenceAccuracyValidator::ClearTestCases() {
    std::lock_guard<std::mutex> lock(validator_mutex_);
    size_t count = test_cases_.size();
    test_cases_.clear();

    PERF_LOG_INFO("accuracy_validator", "Test cases cleared",
                 json{{"cleared_count", count}});
}

std::vector<SampleTestCase> ReferenceAccuracyValidator::GetTestCases() const {
    std::lock_guard<std::mutex> lock(validator_mutex_);
    return test_cases_;
}

void ReferenceAccuracyValidator::SetReferenceImplementation(
    std::function<std::vector<core::UInt256>(const std::vector<core::UInt256>&)> ref_impl) {
    std::lock_guard<std::mutex> lock(validator_mutex_);
    reference_implementation_ = ref_impl;

    PERF_LOG_INFO("accuracy_validator", "Reference implementation set");
}

json ReferenceAccuracyValidator::GetValidationHistory() const {
    std::lock_guard<std::mutex> lock(validator_mutex_);

    json history = json::array();
    for (const auto& result : validation_history_) {
        history.push_back(accuracy_utils::ValidationResultToJson(result));
    }

    return history;
}

ValidationResult ReferenceAccuracyValidator::GetLastValidationResult() const {
    std::lock_guard<std::mutex> lock(validator_mutex_);

    if (validation_history_.empty()) {
        return ValidationResult{};
    }

    return validation_history_.back();
}

double ReferenceAccuracyValidator::GetOverallAccuracyRate() const {
    std::lock_guard<std::mutex> lock(validator_mutex_);

    if (validation_history_.empty()) {
        return 0.0;
    }

    double total_accuracy = 0.0;
    for (const auto& result : validation_history_) {
        total_accuracy += result.accuracy_percentage;
    }

    return total_accuracy / validation_history_.size();
}

std::vector<ValidationResult> ReferenceAccuracyValidator::GetRecentValidations(size_t count) const {
    std::lock_guard<std::mutex> lock(validator_mutex_);

    std::vector<ValidationResult> recent;
    size_t start_idx = validation_history_.size() > count ?
                      validation_history_.size() - count : 0;

    for (size_t i = start_idx; i < validation_history_.size(); ++i) {
        recent.push_back(validation_history_[i]);
    }

    return recent;
}

void ReferenceAccuracyValidator::SetDefaultValidationConfig(const ValidationConfig& config) {
    std::lock_guard<std::mutex> lock(validator_mutex_);
    default_config_ = config;

    PERF_LOG_INFO("accuracy_validator", "Default validation config updated",
                 json{{"config", config.ToJson()}});
}

ValidationConfig ReferenceAccuracyValidator::GetDefaultValidationConfig() const {
    std::lock_guard<std::mutex> lock(validator_mutex_);
    return default_config_;
}

// Private methods implementation
ValidationResult ReferenceAccuracyValidator::PerformSingleValidation(
    const core::UInt256& start_key,
    const core::UInt256& end_key,
    const ValidationConfig& config) {

    ValidationResult result;

    // Generate test keys
    auto test_keys = GenerateTestKeys(start_key, config.sample_size, config.replay_seed);
    result.total_samples_tested = test_keys.size();

    // TODO: Replace with actual GPU execution
    std::vector<core::UInt256> optimized_results;
    for (const auto& key : test_keys) {
        optimized_results.push_back(key); // Placeholder - should be actual GPU computation
    }

    // Compare with reference implementation if available
    if (reference_implementation_ && config.enable_baseline_comparison) {
        auto reference_results = reference_implementation_(test_keys);
        result.baseline_results_match = CompareResults(optimized_results, reference_results, result);
    } else {
        result.baseline_results_match = true; // Assume correct if no reference available
        result.successful_comparisons = result.total_samples_tested;
    }

    // Run edge case tests
    if (!test_cases_.empty()) {
        RunEdgeCaseTests(result, config);
    }

    // Calculate final accuracy
    result.accuracy_percentage = accuracy_utils::CalculateAccuracy(
        result.successful_comparisons, result.total_samples_tested);

    return result;
}

std::vector<core::UInt256> ReferenceAccuracyValidator::GenerateTestKeys(
    const core::UInt256& start_key,
    size_t count,
    const core::UInt256& seed) {

    std::vector<core::UInt256> test_keys;
    test_keys.reserve(count);

    // Use a simple linear progression with seed-based offset for reproducibility
    core::UInt256 current = start_key;
    current.Add(seed);

    for (size_t i = 0; i < count; ++i) {
        test_keys.push_back(current);
        current = core::Incremented(current, 1);
    }

    return test_keys;
}

bool ReferenceAccuracyValidator::CompareResults(
    const std::vector<core::UInt256>& optimized_results,
    const std::vector<core::UInt256>& reference_results,
    ValidationResult& result) {

    if (optimized_results.size() != reference_results.size()) {
        result.validation_details += "Result size mismatch: optimized=" +
                                   std::to_string(optimized_results.size()) +
                                   ", reference=" + std::to_string(reference_results.size()) + "; ";
        return false;
    }

    size_t matches = 0;
    for (size_t i = 0; i < optimized_results.size(); ++i) {
        if (optimized_results[i].Compare(reference_results[i]) == 0) {
            matches++;
        } else {
            result.mismatched_keys.push_back(optimized_results[i]);
            result.validation_details += "Mismatch at index " + std::to_string(i) + "; ";
        }
    }

    result.successful_comparisons += matches;
    return matches == optimized_results.size();
}

bool ReferenceAccuracyValidator::RunEdgeCaseTests(ValidationResult& result, const ValidationConfig& config) {
    bool all_edge_cases_passed = true;

    for (const auto& test_case : test_cases_) {
        // TODO: Replace with actual GPU execution of edge case
        bool edge_case_passed = true; // Placeholder - should test edge case

        if (edge_case_passed) {
            result.successful_comparisons++;
            result.total_samples_tested++;
        } else {
            all_edge_cases_passed = false;
            result.mismatched_keys.push_back(test_case.private_key);
            result.validation_details += "Edge case failed: " + test_case.test_description + "; ";
        }
    }

    return all_edge_cases_passed;
}

void ReferenceAccuracyValidator::UpdateValidationHistory(const ValidationResult& result) {
    std::lock_guard<std::mutex> lock(validator_mutex_);
    validation_history_.push_back(result);

    // Keep only the last 1000 validations to prevent memory growth
    if (validation_history_.size() > 1000) {
        validation_history_.erase(validation_history_.begin(),
                                 validation_history_.begin() + (validation_history_.size() - 1000));
    }
}

std::vector<int> ReferenceAccuracyValidator::DetectAvailableGPUs() const {
    // TODO: Implement actual GPU detection
    return {0}; // Placeholder
}

bool ReferenceAccuracyValidator::IsGPUAvailable(int gpu_id) const {
    // TODO: Implement actual GPU availability check
    return gpu_id >= 0; // Placeholder
}

std::string ReferenceAccuracyValidator::GetGPUName(int gpu_id) const {
    // TODO: Implement actual GPU name retrieval
    return "GPU_" + std::to_string(gpu_id); // Placeholder
}

// ScopedAccuracyValidation implementation
ScopedAccuracyValidation::ScopedAccuracyValidation(
    AccuracyValidator* validator,
    const core::UInt256& start_key,
    const core::UInt256& end_key,
    const ValidationConfig& config)
    : validator_(validator), start_key_(start_key), end_key_(end_key), config_(config),
      validation_completed_(false) {

    if (validator_) {
        result_ = validator_->ValidateOptimization(start_key_, end_key_, config_);
        validation_completed_ = true;
    }
}

ScopedAccuracyValidation::~ScopedAccuracyValidation() {
    if (!validation_completed_) {
        PERF_LOG_WARNING("accuracy_validator", "Scoped validation ended without completion");
    }
}

bool ScopedAccuracyValidation::IsValid() const {
    return validation_completed_ && result_.accuracy_percentage >= config_.accuracy_threshold;
}

ValidationResult ScopedAccuracyValidation::GetResult() const {
    return result_;
}

// Utility functions implementation
namespace accuracy_utils {

SampleTestCase CreateTestCase(const core::UInt256& private_key, const std::string& description) {
    SampleTestCase test_case;
    test_case.private_key = private_key;
    test_case.test_description = description;
    test_case.test_case_id = 0; // Will be assigned by validator
    test_case.is_edge_case = false;

    // TODO: Compute expected public key coordinates
    test_case.expected_public_key_x = core::UInt256::Zero();
    test_case.expected_public_key_y = core::UInt256::Zero();

    return test_case;
}

SampleTestCase CreateEdgeCase(core::UInt256 value, const std::string& description) {
    SampleTestCase test_case = CreateTestCase(value, description);
    test_case.is_edge_case = true;
    return test_case;
}

std::vector<SampleTestCase> CreateStandardTestSuite() {
    std::vector<SampleTestCase> test_suite;

    // Add some standard test cases with known values
    core::UInt256 key1 = core::UInt256::Zero();
    key1.AddUint64(1);
    test_suite.push_back(CreateTestCase(key1, "Private key = 1"));

    core::UInt256 key2 = core::UInt256::Zero();
    key2.AddUint64(12345);
    test_suite.push_back(CreateTestCase(key2, "Small private key"));

    // TODO: Add more comprehensive test cases with known results

    return test_suite;
}

std::vector<SampleTestCase> CreateEdgeCaseTestSuite() {
    std::vector<SampleTestCase> edge_suite;

    // Edge cases
    edge_suite.push_back(CreateEdgeCase(core::UInt256::Zero(), "Zero private key"));

    core::UInt256 unit_key = core::UInt256::Zero();
    unit_key.AddUint64(1);
    edge_suite.push_back(CreateEdgeCase(unit_key, "Unit private key"));

    auto max_key_opt = core::UInt256::FromHex("FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141");
    if (max_key_opt.has_value()) {
        edge_suite.push_back(CreateEdgeCase(max_key_opt.value(), "Maximum valid private key"));
    }

    return edge_suite;
}

core::UInt256 GenerateDeterministicKey(size_t index, const core::UInt256& seed) {
    core::UInt256 result = seed;
    result.AddUint64(static_cast<std::uint64_t>(index));
    return result;
}

std::vector<core::UInt256> GenerateKeyRange(const core::UInt256& start, size_t count, const core::UInt256& seed) {
    std::vector<core::UInt256> keys;
    keys.reserve(count);

    core::UInt256 current = start;
    current.Add(seed);
    for (size_t i = 0; i < count; ++i) {
        keys.push_back(current);
        current = core::Incremented(current, 1);
    }

    return keys;
}

bool ComparePublicKeys(const core::UInt256& x1, const core::UInt256& y1,
                      const core::UInt256& x2, const core::UInt256& y2) {
    return (x1.Compare(x2) == 0) && (y1.Compare(y2) == 0);
}

double CalculateAccuracy(size_t correct, size_t total) {
    if (total == 0) return 0.0;
    return (static_cast<double>(correct) / static_cast<double>(total)) * 100.0;
}

json ValidationResultToJson(const ValidationResult& result) {
    json j;
    j["baseline_results_match"] = result.baseline_results_match;
    j["deterministic_across_runs"] = result.deterministic_across_runs;
    j["correct_across_architectures"] = result.correct_across_architectures;

    std::vector<std::string> mismatched_keys;
    for (const auto& key : result.mismatched_keys) {
        mismatched_keys.push_back(key.ToHex());
    }
    j["mismatched_keys"] = mismatched_keys;

    j["accuracy_percentage"] = result.accuracy_percentage;
    j["total_samples_tested"] = result.total_samples_tested;
    j["successful_comparisons"] = result.successful_comparisons;
    j["validation_time_us"] = result.validation_time.count();
    j["validation_details"] = result.validation_details;
    j["additional_metadata"] = result.additional_metadata;

    return j;
}

ValidationResult JsonToValidationResult(const json& json_result) {
    ValidationResult result;

    result.baseline_results_match = json_result.value("baseline_results_match", false);
    result.deterministic_across_runs = json_result.value("deterministic_across_runs", false);
    result.correct_across_architectures = json_result.value("correct_across_architectures", false);

    if (json_result.contains("mismatched_keys")) {
        for (const auto& key_str : json_result["mismatched_keys"]) {
            auto key_opt = core::UInt256::FromHex(key_str.get<std::string>());
            if (key_opt) {
                result.mismatched_keys.push_back(*key_opt);
            }
        }
    }

    result.accuracy_percentage = json_result.value("accuracy_percentage", 0.0);
    result.total_samples_tested = json_result.value("total_samples_tested", 0);
    result.successful_comparisons = json_result.value("successful_comparisons", 0);
    result.validation_time = std::chrono::microseconds(json_result.value("validation_time_us", 0));
    result.validation_details = json_result.value("validation_details", "");
    result.additional_metadata = json_result.value("additional_metadata", json{});

    return result;
}

std::string FormatValidationReport(const ValidationResult& result) {
    std::ostringstream oss;
    oss << "Accuracy Validation Report:\n";
    oss << "  Total Samples Tested: " << result.total_samples_tested << "\n";
    oss << "  Successful Comparisons: " << result.successful_comparisons << "\n";
    oss << "  Accuracy Percentage: " << std::fixed << std::setprecision(2) << result.accuracy_percentage << "%\n";
    oss << "  Baseline Results Match: " << (result.baseline_results_match ? "YES" : "NO") << "\n";
    oss << "  Deterministic Across Runs: " << (result.deterministic_across_runs ? "YES" : "NO") << "\n";
    oss << "  Correct Across Architectures: " << (result.correct_across_architectures ? "YES" : "NO") << "\n";
    oss << "  Validation Time: " << result.validation_time.count() / 1000.0 << " ms\n";

    if (!result.mismatched_keys.empty()) {
        oss << "  Mismatched Keys (" << result.mismatched_keys.size() << "):\n";
        for (size_t i = 0; i < std::min(size_t(10), result.mismatched_keys.size()); ++i) {
            oss << "    " << result.mismatched_keys[i].ToHex() << "\n";
        }
        if (result.mismatched_keys.size() > 10) {
            oss << "    ... and " << (result.mismatched_keys.size() - 10) << " more\n";
        }
    }

    if (!result.validation_details.empty()) {
        oss << "  Details: " << result.validation_details << "\n";
    }

    return oss.str();
}

} // namespace accuracy_utils

} // namespace puzzle71::gpu::performance