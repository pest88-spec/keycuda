#pragma once

#include <string>
#include <vector>
#include <memory>
#include <map>
#include <chrono>
#include <json/json.h>
#include <cuda_runtime.h>
#include <nvrtc.h>

namespace puzzle71::gpu::performance {

/**
 * @brief CUDA register budget monitoring system for kernel performance optimization
 *
 * Monitors and manages CUDA register usage to ensure kernels stay within the
 * 128 registers per thread budget for optimal performance and occupancy:
 * - Tracks register usage per kernel launch
 * - Provides real-time register usage feedback
 * - Warns when kernels exceed register budget
 * - Suggests optimization strategies for high register usage
 * - Integrates with NVRTC for compile-time register analysis
 * - Maintains historical register usage data
 */

enum class RegisterBudgetStatus {
    WITHIN_BUDGET,        // ≤128 registers/thread - good
    APPROACHING_LIMIT,    // 100-128 registers/thread - warning
    OVER_BUDGET,          // >128 registers/thread - critical
    UNKNOWN               // Unable to determine register usage
};

enum class OptimizationStrategy {
    REDUCE_REGISTER_PRESSURE,  // Reduce register pressure through code changes
    INCREASE_SHARED_MEMORY,    // Use more shared memory to reduce register usage
    LAUNCH_CONFIG_ADJUSTMENT,  // Adjust launch configuration
    KERNEL_REFACTORING,        // Refactor kernel to use fewer registers
    COMPILE_OPTIONS            // Use specific compiler options
};

struct RegisterUsageRecord {
    std::string kernel_name;
    int registers_per_thread;
    int max_threads_per_block;
    double occupancy_achieved;
    RegisterBudgetStatus budget_status;
    std::chrono::system_clock::time_point measurement_time;
    int device_id;
    size_t shared_memory_per_block;
    int block_size;
    size_t grid_size;
    std::vector<OptimizationStrategy> recommended_strategies;
    std::string analysis_notes;
    json metadata;
};

struct RegisterBudgetViolation {
    std::string kernel_name;
    int registers_used;
    int budget_limit;
    double occupancy_impact_percent;
    std::string violation_severity;
    std::chrono::system_clock::time_point detection_time;
    std::vector<std::string> optimization_suggestions;
    bool auto_recovery_attempted;
    bool recovery_successful;
};

struct RegisterUsageAnalysis {
    std::string kernel_name;
    int current_register_usage;
    int optimal_register_usage;
    double current_occupancy;
    double potential_occupancy;
    int performance_impact_score; // 0-100, higher = worse impact
    std::vector<OptimizationStrategy> applicable_strategies;
    std::map<OptimizationStrategy, double> strategy_effectiveness; // Expected occupancy improvement
    std::string analysis_summary;
    bool needs_optimization;
};

struct RegisterBudgetReport {
    std::string report_timestamp;
    int device_id;
    std::string device_name;
    int total_kernels_analyzed;
    int kernels_within_budget;
    int kernels_over_budget;
    int kernels_approaching_limit;
    double average_register_usage;
    double average_occupancy;

    std::vector<RegisterUsageRecord> usage_records;
    std::vector<RegisterBudgetViolation> violations;
    std::vector<RegisterUsageAnalysis> optimization_candidates;

    std::vector<std::string> critical_issues;
    std::vector<std::string> recommendations;
    double overall_compliance_score;
    bool meets_register_budget_requirements;
};

class RegisterBudgetMonitor {
public:
    explicit RegisterBudgetMonitor(int device_id = 0);
    ~RegisterBudgetMonitor();

    // Register usage monitoring
    RegisterUsageRecord MeasureKernelRegisterUsage(const std::string& kernel_name, cudaFunction_t kernel_func);
    RegisterUsageRecord AnalyzeKernelFromNVRTC(const std::string& kernel_name, const std::string& cuda_source, nvrtcProgram program);
    bool CheckKernelRegisterBudget(const std::string& kernel_name, int registers_per_thread);
    RegisterBudgetStatus GetRegisterBudgetStatus(int registers_per_thread) const;

    // Real-time monitoring
    void EnableRealTimeMonitoring(bool enabled);
    void SetMonitoringInterval(std::chrono::milliseconds interval);
    void StartMonitoring();
    void StopMonitoring();
    bool IsMonitoringActive() const;

    // Compilation and analysis
    bool AnalyzeKernelSource(const std::string& source_code, const std::string& kernel_name, RegisterUsageAnalysis& analysis);
    bool GetCompileTimeRegisterUsage(const std::string& source_code, int& register_count);
    std::vector<std::string> GetRegisterPressureIndicators(const std::string& source_code) const;

    // Optimization suggestions
    std::vector<OptimizationStrategy> GetOptimizationStrategies(const RegisterUsageRecord& record) const;
    std::vector<std::string> GenerateOptimizationSuggestions(const RegisterUsageAnalysis& analysis) const;
    bool SuggestLaunchConfigAdjustments(const std::string& kernel_name, int& optimal_block_size, int& optimal_registers) const;

    // Violation handling
    void RegisterBudgetViolation(const RegisterBudgetViolation& violation);
    std::vector<RegisterBudgetViolation> GetActiveViolations() const;
    void ClearViolations();
    bool AttemptAutoRecovery(const RegisterBudgetViolation& violation);

    // Reporting and analytics
    RegisterBudgetReport GenerateBudgetReport() const;
    std::string ExportBudgetReport(const std::string& format = "json") const;
    bool SaveBudgetReport(const std::string& file_path) const;

    // Statistics and trends
    std::vector<RegisterUsageRecord> GetUsageHistory(const std::string& kernel_name,
                                                   std::chrono::hours duration = std::chrono::hours(24)) const;
    double GetAverageRegisterUsage(const std::string& kernel_name) const;
    std::vector<std::string> GetHighRegisterUsageKernels(int threshold = 100) const;
    bool IsRegisterUsageTrendingUp(const std::string& kernel_name) const;

    // Configuration
    void SetRegisterBudgetLimit(int limit); // Default is 128
    void SetWarningThreshold(double threshold); // Default is 0.78 (100/128)
    void SetDeviceId(int device_id);
    void EnableAutoRecovery(bool enabled);
    void SetOptimizationStrategies(const std::vector<OptimizationStrategy>& strategies);

private:
    int device_id_;
    int register_budget_limit_;
    double warning_threshold_;
    bool real_time_monitoring_enabled_;
    bool auto_recovery_enabled_;
    std::chrono::milliseconds monitoring_interval_;
    bool monitoring_active_;

    mutable std::mutex monitor_mutex_;

    // Device properties
    cudaDeviceProp device_properties_;
    std::string device_name_;

    // Historical data
    std::vector<RegisterUsageRecord> usage_history_;
    std::vector<RegisterBudgetViolation> violations_;
    std::map<std::string, std::vector<RegisterUsageRecord>> kernel_usage_map_;

    // Configuration
    std::vector<OptimizationStrategy> enabled_strategies_;

    // Monitoring thread
    std::thread monitoring_thread_;
    std::atomic<bool> stop_monitoring_;

    // Internal methods
    void InitializeDeviceProperties();
    double CalculateOccupancy(int registers_per_thread, int block_size, size_t shared_memory) const;
    int GetMaxThreadsPerBlock(int registers_per_thread) const;
    RegisterBudgetViolation CreateViolationRecord(const RegisterUsageRecord& record) const;
    std::vector<OptimizationStrategy> DetermineApplicableStrategies(const RegisterUsageRecord& record) const;

    // NVRTC integration
    bool ExtractRegisterInfoFromPTX(const std::string& ptx_code, int& register_count) const;
    bool CompileKernelForAnalysis(const std::string& source_code, std::string& ptx_output) const;

    // Source code analysis
    std::vector<std::string> AnalyzeRegisterPressure(const std::string& source_code) const;
    int EstimateRegisterUsageFromSource(const std::string& source_code) const;

    // Optimization analysis
    double EstimateOccupancyImprovement(OptimizationStrategy strategy, const RegisterUsageRecord& record) const;
    std::string GenerateCodeOptimizationSuggestions(const std::string& source_code, int current_registers) const;

    // Monitoring loop
    void MonitoringLoop();
    void PerformPeriodicCheck();

    // Report generation
    double CalculateComplianceScore(const RegisterBudgetReport& report) const;
    std::vector<std::string> GenerateRecommendations(const RegisterBudgetReport& report) const;
    std::vector<std::string> IdentifyCriticalIssues(const RegisterBudgetReport& report) const;

    // Serialization helpers
    json RegisterUsageRecordToJson(const RegisterUsageRecord& record) const;
    json RegisterBudgetViolationToJson(const RegisterBudgetViolation& violation) const;
    json RegisterUsageAnalysisToJson(const RegisterUsageAnalysis& analysis) const;
    json RegisterBudgetReportToJson(const RegisterBudgetReport& report) const;
};

/**
 * @brief Scoped register budget validator for automatic kernel monitoring
 */
class ScopedRegisterBudgetValidator {
public:
    ScopedRegisterBudgetValidator(RegisterBudgetMonitor& monitor,
                                 const std::string& kernel_name,
                                 cudaFunction_t kernel_func);
    ~ScopedRegisterBudgetValidator();

    bool IsWithinBudget() const;
    int GetRegisterUsage() const;
    RegisterBudgetStatus GetBudgetStatus() const;

private:
    RegisterBudgetMonitor& monitor_;
    std::string kernel_name_;
    RegisterUsageRecord usage_record_;
    bool validation_completed_;
};

/**
 * @brief Utility class for register budget optimization strategies
 */
class RegisterBudgetOptimizer {
public:
    static std::string OptimizeKernelForRegisterUsage(const std::string& source_code, int target_registers);
    static std::vector<std::string> SuggestCodeRefactoring(const std::string& source_code, int current_registers);
    static std::string GenerateOptimizedLaunchParams(const std::string& kernel_name, int current_registers, int target_registers);
    static bool ValidateOptimizationEffectiveness(const std::string& original_code, const std::string& optimized_code);

private:
    static std::string ReduceRegisterPressureTechniques(const std::string& source_code);
    static std::string IncreaseSharedMemoryUsage(const std::string& source_code);
    static std::string ApplyCompilerOptimizations(const std::string& source_code);
};

/**
 * @brief Factory function to create and configure register budget monitor
 */
std::unique_ptr<RegisterBudgetMonitor> CreateRegisterBudgetMonitor(
    int device_id = 0,
    int register_budget_limit = 128,
    bool enable_real_time_monitoring = true);

} // namespace puzzle71::gpu::performance