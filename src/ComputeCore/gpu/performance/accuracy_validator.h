#pragma once

#include <vector>
#include <memory>
#include <string>
#include <functional>
#include <chrono>
#include <random>
#include <nlohmann/json.hpp>
#include "core/uint256.h"
#include "ComputeCore/gpu/performance/performance_logger.h"

namespace puzzle71::gpu::performance {

using json = nlohmann::json;

struct ValidationResult {
    bool baseline_results_match;                   // Comparison with baseline implementation
    bool deterministic_across_runs;                // Reproducible results across multiple runs
    bool correct_across_architectures;             // Cross-GPU consistency validation
    std::vector<core::UInt256> mismatched_keys;    // Any incorrect results found
    double accuracy_percentage;                    // 100.0% required for cryptographic operations
    size_t total_samples_tested;                   // Total number of test samples
    size_t successful_comparisons;                 // Number of successful comparisons
    std::chrono::microseconds validation_time;     // Time taken for validation
    std::string validation_details;                // Detailed validation information
    json additional_metadata;                      // Extra validation data

    ValidationResult()
        : baseline_results_match(false), deterministic_across_runs(false),
          correct_across_architectures(false), accuracy_percentage(0.0),
          total_samples_tested(0), successful_comparisons(0),
          validation_time(std::chrono::microseconds(0)) {}
};

struct ValidationConfig {
    size_t sample_size = 10000;                    // Number of keys to validate
    bool enable_cross_gpu_validation = true;       // Test across multiple GPUs
    bool enable_determinism_testing = true;        // Test reproducible results
    bool enable_baseline_comparison = true;        // Compare with reference implementation
    int num_determinism_runs = 5;                  // Number of runs for determinism testing
    double accuracy_threshold = 100.0;             // Required accuracy percentage
    std::vector<int> target_gpu_ids;               // GPUs to test (empty = all available)
    core::UInt256 replay_seed;                     // Seed for reproducible testing
    bool enable_detailed_logging = true;           // Log detailed validation information
    bool enable_performance_validation = true;     // Validate that optimizations don't degrade performance
    json additional_metadata;                      // Additional configuration metadata

    json ToJson() const {
        json config;
        config["sample_size"] = sample_size;
        config["enable_cross_gpu_validation"] = enable_cross_gpu_validation;
        config["enable_determinism_testing"] = enable_determinism_testing;
        config["enable_baseline_comparison"] = enable_baseline_comparison;
        config["num_determinism_runs"] = num_determinism_runs;
        config["accuracy_threshold"] = accuracy_threshold;
        config["target_gpu_ids"] = target_gpu_ids;
        config["replay_seed"] = replay_seed.ToHex();
        config["enable_detailed_logging"] = enable_detailed_logging;
        config["enable_performance_validation"] = enable_performance_validation;
        return config;
    }
};

struct SampleTestCase {
    core::UInt256 private_key;
    core::UInt256 expected_public_key_x;
    core::UInt256 expected_public_key_y;
    std::string test_description;
    int test_case_id;
    bool is_edge_case;                             // Special edge case for testing

    SampleTestCase() : test_case_id(0), is_edge_case(false) {}
};

class AccuracyValidator {
public:
    static std::unique_ptr<AccuracyValidator> Create();

    // Primary validation interface
    virtual ValidationResult ValidateOptimization(
        const core::UInt256& start_key,
        const core::UInt256& end_key,
        const ValidationConfig& config = ValidationConfig{}
    ) = 0;

    // Cross-validation across different GPU architectures
    virtual ValidationResult CrossValidateArchitectures(
        const std::vector<int>& gpu_ids,
        const core::UInt256& start_key,
        const core::UInt256& end_key,
        const ValidationConfig& config = ValidationConfig{}
    ) = 0;

    // Deterministic execution validation
    virtual ValidationResult ValidateDeterminism(
        const core::UInt256& start_key,
        const core::UInt256& end_key,
        int num_runs = 5,
        const core::UInt256& replay_seed = core::UInt256::Zero()
    ) = 0;

    // T017a: Synchronization optimization accuracy validation
    virtual ValidationResult ValidateSynchronizationOptimization(
        const core::UInt256& start_key,
        const core::UInt256& end_key,
        uint64_t replay_seed = 0,
        bool enable_detailed_timing_analysis = true
    ) = 0;

    // T017b: Adaptive parallelism scaling accuracy validation
    virtual ValidationResult ValidateAdaptiveParallelismScaling(
        const core::UInt256& start_key,
        const core::UInt256& end_key,
        const std::vector<int>& points_per_thread_values = {64, 128, 256},
        const core::UInt256& replay_seed = core::UInt256::Zero()
    ) = 0;

    // T017c: Memory access optimization accuracy validation
    virtual ValidationResult ValidateMemoryAccessOptimizations(
        const core::UInt256& start_key,
        const core::UInt256& end_key,
        const std::vector<std::string>& optimization_levels = {"baseline", "optimized"},
        bool enable_cross_gpu_consistency_checks = true
    ) = 0;

    // Enhanced cross-architecture validation with <0.001% error tolerance
    virtual ValidationResult ValidateCrossArchitectureWithTolerance(
        const std::vector<int>& gpu_ids,
        const core::UInt256& start_key,
        const core::UInt256& end_key,
        double error_tolerance_percent = 0.001,
        const core::UInt256& replay_seed = core::UInt256::Zero()
    ) = 0;

    // Statistical validation with p-value analysis
    virtual ValidationResult ValidateStatisticalSignificance(
        const core::UInt256& start_key,
        const core::UInt256& end_key,
        int num_runs = 100,
        double significance_level = 0.001,
        const core::UInt256& replay_seed = core::UInt256::Zero()
    ) = 0;

    // Batch validation with multiple test cases
    virtual ValidationResult ValidateBatch(
        const std::vector<SampleTestCase>& test_cases,
        const ValidationConfig& config = ValidationConfig{}
    ) = 0;

    // Continuous accuracy monitoring
    virtual bool EnableContinuousValidation() = 0;
    virtual void DisableContinuousValidation() = 0;
    virtual bool IsContinuousValidationEnabled() const = 0;

    // Test case management
    virtual void AddTestCase(const SampleTestCase& test_case) = 0;
    virtual void AddEdgeCaseTest(const core::UInt256& private_key, const std::string& description) = 0;
    virtual void ClearTestCases() = 0;
    virtual std::vector<SampleTestCase> GetTestCases() const = 0;

    // Reference implementation access
    virtual void SetReferenceImplementation(
        std::function<std::vector<core::UInt256>(const std::vector<core::UInt256>&)> ref_impl
    ) = 0;

    // Validation statistics and reporting
    virtual json GetValidationHistory() const = 0;
    virtual ValidationResult GetLastValidationResult() const = 0;
    virtual double GetOverallAccuracyRate() const = 0;
    virtual std::vector<ValidationResult> GetRecentValidations(size_t count = 10) const = 0;

    // Configuration
    virtual void SetDefaultValidationConfig(const ValidationConfig& config) = 0;
    virtual ValidationConfig GetDefaultValidationConfig() const = 0;

    virtual ~AccuracyValidator() = default;

protected:
    ValidationConfig default_config_;
    std::vector<SampleTestCase> test_cases_;
    std::vector<ValidationResult> validation_history_;
    bool continuous_validation_enabled_ = false;
    mutable std::mutex validator_mutex_;
};

// Reference accuracy validator implementation
class ReferenceAccuracyValidator : public AccuracyValidator {
public:
    ReferenceAccuracyValidator();
    ~ReferenceAccuracyValidator() override = default;

    // AccuracyValidator interface implementation
    ValidationResult ValidateOptimization(
        const core::UInt256& start_key,
        const core::UInt256& end_key,
        const ValidationConfig& config = ValidationConfig{}
    ) override;

    ValidationResult CrossValidateArchitectures(
        const std::vector<int>& gpu_ids,
        const core::UInt256& start_key,
        const core::UInt256& end_key,
        const ValidationConfig& config = ValidationConfig{}
    ) override;

    ValidationResult ValidateDeterminism(
        const core::UInt256& start_key,
        const core::UInt256& end_key,
        int num_runs = 5,
        const core::UInt256& replay_seed = core::UInt256::Zero()
    ) override;

    // T017a: Synchronization optimization accuracy validation
    ValidationResult ValidateSynchronizationOptimization(
        const core::UInt256& start_key,
        const core::UInt256& end_key,
        uint64_t replay_seed = 0,
        bool enable_detailed_timing_analysis = true
    ) override;

    // T017b: Adaptive parallelism scaling accuracy validation
    ValidationResult ValidateAdaptiveParallelismScaling(
        const core::UInt256& start_key,
        const core::UInt256& end_key,
        const std::vector<int>& points_per_thread_values = {64, 128, 256},
        const core::UInt256& replay_seed = core::UInt256::Zero()
    ) override;

    // T017c: Memory access optimization accuracy validation
    ValidationResult ValidateMemoryAccessOptimizations(
        const core::UInt256& start_key,
        const core::UInt256& end_key,
        const std::vector<std::string>& optimization_levels = {"baseline", "optimized"},
        bool enable_cross_gpu_consistency_checks = true
    ) override;

    // Enhanced cross-architecture validation with <0.001% error tolerance
    ValidationResult ValidateCrossArchitectureWithTolerance(
        const std::vector<int>& gpu_ids,
        const core::UInt256& start_key,
        const core::UInt256& end_key,
        double error_tolerance_percent = 0.001,
        const core::UInt256& replay_seed = core::UInt256::Zero()
    ) override;

    // Statistical validation with p-value analysis
    ValidationResult ValidateStatisticalSignificance(
        const core::UInt256& start_key,
        const core::UInt256& end_key,
        int num_runs = 100,
        double significance_level = 0.001,
        const core::UInt256& replay_seed = core::UInt256::Zero()
    ) override;

    ValidationResult ValidateBatch(
        const std::vector<SampleTestCase>& test_cases,
        const ValidationConfig& config = ValidationConfig{}
    ) override;

    bool EnableContinuousValidation() override;
    void DisableContinuousValidation() override;
    bool IsContinuousValidationEnabled() const override;

    void AddTestCase(const SampleTestCase& test_case) override;
    void AddEdgeCaseTest(const core::UInt256& private_key, const std::string& description) override;
    void ClearTestCases() override;
    std::vector<SampleTestCase> GetTestCases() const override;

    void SetReferenceImplementation(
        std::function<std::vector<core::UInt256>(const std::vector<core::UInt256>&)> ref_impl
    ) override;

    json GetValidationHistory() const override;
    ValidationResult GetLastValidationResult() const override;
    double GetOverallAccuracyRate() const override;
    std::vector<ValidationResult> GetRecentValidations(size_t count = 10) const override;

    void SetDefaultValidationConfig(const ValidationConfig& config) override;
    ValidationConfig GetDefaultValidationConfig() const override;

private:
    // Internal validation methods
    ValidationResult PerformSingleValidation(
        const core::UInt256& start_key,
        const core::UInt256& end_key,
        const ValidationConfig& config
    );

    std::vector<core::UInt256> GenerateTestKeys(
        const core::UInt256& start_key,
        size_t count,
        const core::UInt256& seed
    );

    bool CompareResults(
        const std::vector<core::UInt256>& optimized_results,
        const std::vector<core::UInt256>& reference_results,
        ValidationResult& result
    );

    bool RunEdgeCaseTests(ValidationResult& result, const ValidationConfig& config);

    void UpdateValidationHistory(const ValidationResult& result);

    // GPU-specific validation
    std::vector<int> DetectAvailableGPUs() const;
    bool IsGPUAvailable(int gpu_id) const;
    std::string GetGPUName(int gpu_id) const;

    // Test case generation
    std::vector<SampleTestCase> GenerateStandardTestCases(size_t count);
    std::vector<SampleTestCase> GenerateEdgeCaseTestCases();

    // Result analysis
    double CalculateAccuracyPercentage(const ValidationResult& result) const;
    json AnalyzeValidationResults(const std::vector<ValidationResult>& results) const;

    std::function<std::vector<core::UInt256>(const std::vector<core::UInt256>&)> reference_implementation_;
};

// RAII helper for automatic validation during testing
class ScopedAccuracyValidation {
public:
    ScopedAccuracyValidation(AccuracyValidator* validator,
                            const core::UInt256& start_key,
                            const core::UInt256& end_key,
                            const ValidationConfig& config = ValidationConfig{});
    ~ScopedAccuracyValidation();

    bool IsValid() const;
    ValidationResult GetResult() const;

private:
    AccuracyValidator* validator_;
    core::UInt256 start_key_;
    core::UInt256 end_key_;
    ValidationConfig config_;
    ValidationResult result_;
    bool validation_completed_;
};

// Utility functions for accuracy validation
namespace accuracy_utils {
    SampleTestCase CreateTestCase(const core::UInt256& private_key, const std::string& description = "");
    SampleTestCase CreateEdgeCase(core::UInt256 value, const std::string& description);
    std::vector<SampleTestCase> CreateStandardTestSuite();
    std::vector<SampleTestCase> CreateEdgeCaseTestSuite();

    core::UInt256 GenerateDeterministicKey(size_t index, const core::UInt256& seed);
    std::vector<core::UInt256> GenerateKeyRange(const core::UInt256& start, size_t count, const core::UInt256& seed);

    bool ComparePublicKeys(const core::UInt256& x1, const core::UInt256& y1,
                          const core::UInt256& x2, const core::UInt256& y2);
    double CalculateAccuracy(size_t correct, size_t total);

    json ValidationResultToJson(const ValidationResult& result);
    ValidationResult JsonToValidationResult(const json& json_result);
    std::string FormatValidationReport(const ValidationResult& result);
}

} // namespace puzzle71::gpu::performance