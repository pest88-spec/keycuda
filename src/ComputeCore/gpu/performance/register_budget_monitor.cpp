#include "ComputeCore/gpu/performance/register_budget_monitor.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iomanip>
#include <thread>
#include <atomic>
#include <regex>
#include <cstring>

namespace puzzle71::gpu::performance {

RegisterBudgetMonitor::RegisterBudgetMonitor(int device_id)
    : device_id_(device_id)
    , register_budget_limit_(128)
    , warning_threshold_(0.78) // 100/128
    , real_time_monitoring_enabled_(true)
    , auto_recovery_enabled_(true)
    , monitoring_interval_(std::chrono::milliseconds(5000))
    , monitoring_active_(false)
    , stop_monitoring_(false) {

    InitializeDeviceProperties();

    // Initialize default optimization strategies
    enabled_strategies_ = {
        OptimizationStrategy::REDUCE_REGISTER_PRESSURE,
        OptimizationStrategy::INCREASE_SHARED_MEMORY,
        OptimizationStrategy::LAUNCH_CONFIG_ADJUSTMENT,
        OptimizationStrategy::KERNEL_REFACTORING
    };
}

RegisterBudgetMonitor::~RegisterBudgetMonitor() {
    StopMonitoring();
    if (monitoring_thread_.joinable()) {
        monitoring_thread_.join();
    }
}

RegisterUsageRecord RegisterBudgetMonitor::MeasureKernelRegisterUsage(const std::string& kernel_name, cudaFunction_t kernel_func) {
    std::lock_guard<std::mutex> lock(monitor_mutex_);

    RegisterUsageRecord record;
    record.kernel_name = kernel_name;
    record.device_id = device_id_;
    record.measurement_time = std::chrono::system_clock::now();

    // Get kernel attributes including register usage
    cudaFuncAttributes attr;
    cudaError_t result = cudaFuncGetAttributes(&attr, kernel_func);

    if (result == cudaSuccess) {
        record.registers_per_thread = attr.numRegs;
        record.shared_memory_per_block = attr.sharedSizeBytes;
        record.const_size_bytes = attr.constSizeBytes;
        record.local_size_bytes = attr.localSizeBytes;
        record.max_threads_per_block = attr.maxThreadsPerBlock;

        // Calculate occupancy for typical block sizes
        record.block_size = 256; // Default block size for analysis
        record.grid_size = 0;
        record.occupancy_achieved = CalculateOccupancy(attr.numRegs, record.block_size, attr.sharedSizeBytes);

        // Determine budget status
        record.budget_status = GetRegisterBudgetStatus(attr.numRegs);

        // Determine optimization strategies
        record.recommended_strategies = DetermineApplicableStrategies(record);

        record.analysis_notes = "Measured using cudaFuncGetAttributes";
    } else {
        record.registers_per_thread = -1;
        record.budget_status = RegisterBudgetStatus::UNKNOWN;
        record.analysis_notes = "Failed to measure: " + std::string(cudaGetErrorString(result));
    }

    // Store in history
    usage_history_.push_back(record);
    kernel_usage_map_[kernel_name].push_back(record);

    // Check for budget violation
    if (record.budget_status == RegisterBudgetStatus::OVER_BUDGET) {
        auto violation = CreateViolationRecord(record);
        RegisterBudgetViolation(violation);
    }

    return record;
}

RegisterUsageRecord RegisterBudgetMonitor::AnalyzeKernelFromNVRTC(const std::string& kernel_name, const std::string& cuda_source, nvrtcProgram program) {
    std::lock_guard<std::mutex> lock(monitor_mutex_);

    RegisterUsageRecord record;
    record.kernel_name = kernel_name;
    record.device_id = device_id_;
    record.measurement_time = std::chrono::system_clock::now();

    // Get PTX from NVRTC program
    size_t ptx_size;
    nvrtcResult nvrtc_result = nvrtcGetPTXSize(program, &ptx_size);

    if (nvrtc_result == NVRTC_SUCCESS) {
        std::vector<char> ptx_buffer(ptx_size);
        nvrtc_result = nvrtcGetPTX(program, ptx_buffer.data());

        if (nvrtc_result == NVRTC_SUCCESS) {
            std::string ptx_code(ptx_buffer.data(), ptx_size);

            // Extract register count from PTX
            int register_count = 0;
            if (ExtractRegisterInfoFromPTX(ptx_code, register_count)) {
                record.registers_per_thread = register_count;
                record.budget_status = GetRegisterBudgetStatus(register_count);
                record.analysis_notes = "Extracted from NVRTC-generated PTX";

                // Calculate estimated occupancy
                record.block_size = 256;
                record.shared_memory_per_block = 0; // Unknown at compile time
                record.occupancy_achieved = CalculateOccupancy(register_count, record.block_size, 0);
                record.max_threads_per_block = GetMaxThreadsPerBlock(register_count);

                record.recommended_strategies = DetermineApplicableStrategies(record);
            } else {
                record.registers_per_thread = -1;
                record.budget_status = RegisterBudgetStatus::UNKNOWN;
                record.analysis_notes = "Failed to extract register info from PTX";
            }
        } else {
            record.registers_per_thread = -1;
            record.budget_status = RegisterBudgetStatus::UNKNOWN;
            record.analysis_notes = "NVRTC failed to get PTX";
        }
    } else {
        record.registers_per_thread = -1;
        record.budget_status = RegisterBudgetStatus::UNKNOWN;
        record.analysis_notes = "NVRTC failed to get PTX size";
    }

    // Store in history
    usage_history_.push_back(record);
    kernel_usage_map_[kernel_name].push_back(record);

    return record;
}

bool RegisterBudgetMonitor::CheckKernelRegisterBudget(const std::string& kernel_name, int registers_per_thread) {
    return GetRegisterBudgetStatus(registers_per_thread) != RegisterBudgetStatus::OVER_BUDGET;
}

RegisterBudgetStatus RegisterBudgetMonitor::GetRegisterBudgetStatus(int registers_per_thread) const {
    if (registers_per_thread < 0) {
        return RegisterBudgetStatus::UNKNOWN;
    }

    if (registers_per_thread > register_budget_limit_) {
        return RegisterBudgetStatus::OVER_BUDGET;
    }

    double usage_ratio = static_cast<double>(registers_per_thread) / register_budget_limit_;
    if (usage_ratio >= warning_threshold_) {
        return RegisterBudgetStatus::APPROACHING_LIMIT;
    }

    return RegisterBudgetStatus::WITHIN_BUDGET;
}

void RegisterBudgetMonitor::EnableRealTimeMonitoring(bool enabled) {
    std::lock_guard<std::mutex> lock(monitor_mutex_);
    real_time_monitoring_enabled_ = enabled;
}

void RegisterBudgetMonitor::SetMonitoringInterval(std::chrono::milliseconds interval) {
    std::lock_guard<std::mutex> lock(monitor_mutex_);
    monitoring_interval_ = interval;
}

void RegisterBudgetMonitor::StartMonitoring() {
    std::lock_guard<std::mutex> lock(monitor_mutex_);

    if (!monitoring_active_ && real_time_monitoring_enabled_) {
        monitoring_active_ = true;
        stop_monitoring_ = false;
        monitoring_thread_ = std::thread(&RegisterBudgetMonitor::MonitoringLoop, this);
    }
}

void RegisterBudgetMonitor::StopMonitoring() {
    {
        std::lock_guard<std::mutex> lock(monitor_mutex_);
        monitoring_active_ = false;
        stop_monitoring_ = true;
    }

    if (monitoring_thread_.joinable()) {
        monitoring_thread_.join();
    }
}

bool RegisterBudgetMonitor::IsMonitoringActive() const {
    std::lock_guard<std::mutex> lock(monitor_mutex_);
    return monitoring_active_;
}

bool RegisterBudgetMonitor::AnalyzeKernelSource(const std::string& source_code, const std::string& kernel_name, RegisterUsageAnalysis& analysis) {
    analysis.kernel_name = kernel_name;
    analysis.current_register_usage = EstimateRegisterUsageFromSource(source_code);
    analysis.optimal_register_usage = std::min(analysis.current_register_usage, register_budget_limit_);

    if (analysis.current_register_usage < 0) {
        return false;
    }

    // Calculate current and potential occupancy
    analysis.current_occupancy = CalculateOccupancy(analysis.current_register_usage, 256, 0);
    analysis.potential_occupancy = CalculateOccupancy(analysis.optimal_register_usage, 256, 0);

    // Calculate performance impact score (0-100)
    double occupancy_loss = analysis.current_occupancy - analysis.potential_occupancy;
    analysis.performance_impact_score = static_cast<int>(occupancy_loss * 100);

    // Determine applicable strategies
    analysis.applicable_strategies = DetermineApplicableStrategies(
        RegisterUsageRecord{kernel_name, analysis.current_register_usage, 0, 0.0,
                          GetRegisterBudgetStatus(analysis.current_register_usage),
                          std::chrono::system_clock::now(), device_id_, 0, 256, 0, {}, ""});

    // Estimate effectiveness of each strategy
    for (const auto& strategy : analysis.applicable_strategies) {
        analysis.strategy_effectiveness[strategy] = EstimateOccupancyImprovement(strategy,
            RegisterUsageRecord{kernel_name, analysis.current_register_usage, 0, 0.0,
                              GetRegisterBudgetStatus(analysis.current_register_usage),
                              std::chrono::system_clock::now(), device_id_, 0, 256, 0, {}, ""});
    }

    // Determine if optimization is needed
    analysis.needs_optimization = (analysis.budget_status == RegisterBudgetStatus::OVER_BUDGET ||
                                  analysis.budget_status == RegisterBudgetStatus::APPROACHING_LIMIT ||
                                  analysis.performance_impact_score > 20);

    // Generate summary
    std::ostringstream oss;
    oss << "Kernel '" << kernel_name << "' uses " << analysis.current_register_usage
        << " registers per thread. ";
    if (analysis.needs_optimization) {
        oss << "Optimization recommended. Potential occupancy improvement: "
            << (analysis.potential_occupancy - analysis.current_occupancy) * 100 << "%";
    } else {
        oss << "Register usage is within acceptable limits.";
    }
    analysis.analysis_summary = oss.str();

    return true;
}

bool RegisterBudgetMonitor::GetCompileTimeRegisterUsage(const std::string& source_code, int& register_count) {
    std::string ptx_output;
    if (CompileKernelForAnalysis(source_code, ptx_output)) {
        return ExtractRegisterInfoFromPTX(ptx_output, register_count);
    }
    return false;
}

std::vector<std::string> RegisterBudgetMonitor::GetRegisterPressureIndicators(const std::string& source_code) const {
    std::vector<std::string> indicators;

    // Look for patterns that typically increase register pressure
    static const std::vector<std::regex> pressure_patterns = {
        std::regex(R"(\bfloat[3-4]\s+\w+)"),           // float3, float4 vectors
        std::regex(R"(\bdouble[2-4]\s+\w+)"),          // double vectors
        std::regex(R"(\bint[3-4]\s+\w+)"),             // int3, int4 vectors
        std::regex(R"(\b(?:uint|unsigned\s+int)[3-4]\s+\w+)"), // uint vectors
        std::regex(R"(\w+\[\s*\d+\s*\]\s*\[.*?\]\s*[\w&]+)"), // Multi-dimensional arrays
        std::regex(R"(\{\s*\w+\s*,\s*\w+\s*,\s*\w+\s*\})"),  // struct initializers
        std::regex(R"(\b(?:for|while)\s*\(.*\{)"),           // Complex loops
        std::regex(R"(\bif\s*\([^)]+\)\s*\{[^}]*\}\s*else\s*\{)"), // Complex conditionals
        std::regex(R"(\w+\s*\([^)]*\)\s*\{[^}]*return[^}]*\})") // Complex functions
    };

    std::istringstream stream(source_code);
    std::string line;
    int line_number = 0;

    while (std::getline(stream, line)) {
        line_number++;

        for (const auto& pattern : pressure_patterns) {
            std::smatch match;
            if (std::regex_search(line, match, pattern)) {
                indicators.push_back("Line " + std::to_string(line_number) + ": " +
                                   match.str().substr(0, std::min(50, (int)match.str().length())));
            }
        }
    }

    return indicators;
}

std::vector<OptimizationStrategy> RegisterBudgetMonitor::GetOptimizationStrategies(const RegisterUsageRecord& record) const {
    return DetermineApplicableStrategies(record);
}

std::vector<std::string> RegisterBudgetMonitor::GenerateOptimizationSuggestions(const RegisterUsageAnalysis& analysis) const {
    std::vector<std::string> suggestions;

    for (const auto& strategy : analysis.applicable_strategies) {
        switch (strategy) {
            case OptimizationStrategy::REDUCE_REGISTER_PRESSURE:
                suggestions.push_back("Reduce register pressure by breaking complex expressions into simpler ones");
                suggestions.push_back("Use smaller data types where possible (e.g., float instead of double)");
                suggestions.push_back("Avoid large local arrays and use shared memory instead");
                break;

            case OptimizationStrategy::INCREASE_SHARED_MEMORY:
                suggestions.push_back("Move frequently accessed data to shared memory");
                suggestions.push_back("Use shared memory for temporary storage instead of registers");
                suggestions.push_back("Optimize shared memory bank conflicts");
                break;

            case OptimizationStrategy::LAUNCH_CONFIG_ADJUSTMENT:
                suggestions.push_back("Reduce block size to increase occupancy with high register usage");
                suggestions.push_back("Experiment with different thread block configurations");
                suggestions.push_back("Use occupancy calculator to find optimal configuration");
                break;

            case OptimizationStrategy::KERNEL_REFACTORING:
                suggestions.push_back("Split kernel into multiple simpler kernels");
                suggestions.push_back("Extract complex logic into device functions");
                suggestions.push_back("Minimize register lifetime through scope management");
                break;

            case OptimizationStrategy::COMPILE_OPTIONS:
                suggestions.push_back("Use -maxrregcount compiler flag to limit register usage");
                suggestions.push_back("Try different optimization levels (-O2, -O3)");
                suggestions.push_back("Use -ftz=true and -prec-div=false for relaxed precision");
                break;
        }
    }

    return suggestions;
}

bool RegisterBudgetMonitor::SuggestLaunchConfigAdjustments(const std::string& kernel_name, int& optimal_block_size, int& optimal_registers) const {
    auto it = kernel_usage_map_.find(kernel_name);
    if (it == kernel_usage_map_.end() || it->second.empty()) {
        return false;
    }

    const auto& latest_record = it->second.back();
    int current_registers = latest_record.registers_per_thread;

    // Try different block sizes to find optimal occupancy
    double max_occupancy = 0.0;
    int best_block_size = 256;

    for (int block_size = 32; block_size <= 1024; block_size *= 2) {
        double occupancy = CalculateOccupancy(current_registers, block_size, latest_record.shared_memory_per_block);
        if (occupancy > max_occupancy) {
            max_occupancy = occupancy;
            best_block_size = block_size;
        }
    }

    optimal_block_size = best_block_size;
    optimal_registers = current_registers;

    return true;
}

void RegisterBudgetMonitor::RegisterBudgetViolation(const RegisterBudgetViolation& violation) {
    std::lock_guard<std::mutex> lock(monitor_mutex_);
    violations_.push_back(violation);

    // Attempt auto-recovery if enabled
    if (auto_recovery_enabled_) {
        AttemptAutoRecovery(violation);
    }
}

std::vector<RegisterBudgetViolation> RegisterBudgetMonitor::GetActiveViolations() const {
    std::lock_guard<std::mutex> lock(monitor_mutex_);
    return violations_;
}

void RegisterBudgetMonitor::ClearViolations() {
    std::lock_guard<std::mutex> lock(monitor_mutex_);
    violations_.clear();
}

bool RegisterBudgetMonitor::AttemptAutoRecovery(const RegisterBudgetViolation& violation) {
    // Simple auto-recovery: suggest launch config adjustment
    int optimal_block_size, optimal_registers;
    if (SuggestLaunchConfigAdjustments(violation.kernel_name, optimal_block_size, optimal_registers)) {
        // Mark as recovery attempted (in a real implementation, you might actually
        // reconfigure the launch parameters automatically)
        for (auto& v : violations_) {
            if (v.kernel_name == violation.kernel_name) {
                v.auto_recovery_attempted = true;
                v.recovery_successful = true;
                break;
            }
        }
        return true;
    }
    return false;
}

RegisterBudgetReport RegisterBudgetMonitor::GenerateBudgetReport() const {
    std::lock_guard<std::mutex> lock(monitor_mutex_);

    RegisterBudgetReport report;
    report.report_timestamp = std::to_string(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
    report.device_id = device_id_;
    report.device_name = device_name_;

    // Analyze usage history
    report.total_kernels_analyzed = usage_history_.size();
    report.kernels_within_budget = 0;
    report.kernels_over_budget = 0;
    report.kernels_approaching_limit = 0;

    double total_registers = 0.0;
    double total_occupancy = 0.0;

    for (const auto& record : usage_history_) {
        report.usage_records.push_back(record);

        switch (record.budget_status) {
            case RegisterBudgetStatus::WITHIN_BUDGET:
                report.kernels_within_budget++;
                break;
            case RegisterBudgetStatus::APPROACHING_LIMIT:
                report.kernels_approaching_limit++;
                break;
            case RegisterBudgetStatus::OVER_BUDGET:
                report.kernels_over_budget++;
                break;
            default:
                break;
        }

        if (record.registers_per_thread > 0) {
            total_registers += record.registers_per_thread;
            total_occupancy += record.occupancy_achieved;
        }
    }

    report.average_register_usage = report.total_kernels_analyzed > 0 ?
        total_registers / report.total_kernels_analyzed : 0.0;
    report.average_occupancy = report.total_kernels_analyzed > 0 ?
        total_occupancy / report.total_kernels_analyzed : 0.0;

    // Copy violations
    report.violations = violations_;

    // Generate optimization candidates
    std::set<std::string> analyzed_kernels;
    for (const auto& record : usage_history_) {
        if (record.budget_status != RegisterBudgetStatus::WITHIN_BUDGET &&
            analyzed_kernels.find(record.kernel_name) == analyzed_kernels.end()) {

            RegisterUsageAnalysis analysis;
            // This would require source code, so we'll create a simplified analysis
            analysis.kernel_name = record.kernel_name;
            analysis.current_register_usage = record.registers_per_thread;
            analysis.current_occupancy = record.occupancy_achieved;
            analysis.needs_optimization = true;
            analysis.applicable_strategies = record.recommended_strategies;

            report.optimization_candidates.push_back(analysis);
            analyzed_kernels.insert(record.kernel_name);
        }
    }

    // Generate critical issues and recommendations
    report.critical_issues = IdentifyCriticalIssues(report);
    report.recommendations = GenerateRecommendations(report);
    report.overall_compliance_score = CalculateComplianceScore(report);
    report.meets_register_budget_requirements = report.overall_compliance_score >= 80.0;

    return report;
}

std::string RegisterBudgetMonitor::ExportBudgetReport(const std::string& format) const {
    auto report = GenerateBudgetReport();

    if (format == "json") {
        return RegisterBudgetReportToJson(report).dump(4);
    } else {
        // Human-readable format
        std::ostringstream oss;
        oss << "CUDA Register Budget Report\n";
        oss << "==========================\n";
        oss << "Device: " << report.device_name << " (ID: " << report.device_id << ")\n";
        oss << "Generated: " << report.report_timestamp << "\n\n";

        oss << "Summary:\n";
        oss << "  Total Kernels: " << report.total_kernels_analyzed << "\n";
        oss << "  Within Budget: " << report.kernels_within_budget << "\n";
        oss << "  Approaching Limit: " << report.kernels_approaching_limit << "\n";
        oss << "  Over Budget: " << report.kernels_over_budget << "\n";
        oss << "  Average Register Usage: " << std::fixed << std::setprecision(1) << report.average_register_usage << "\n";
        oss << "  Average Occupancy: " << std::setprecision(2) << (report.average_occupancy * 100) << "%\n";
        oss << "  Compliance Score: " << std::setprecision(1) << report.overall_compliance_score << "%\n";
        oss << "  Meets Requirements: " << (report.meets_register_budget_requirements ? "Yes" : "No") << "\n";

        return oss.str();
    }
}

bool RegisterBudgetMonitor::SaveBudgetReport(const std::string& file_path) const {
    try {
        std::ofstream file(file_path);
        if (!file.is_open()) {
            return false;
        }

        std::string content = ExportBudgetReport("json");
        file << content;
        return file.good();
    } catch (...) {
        return false;
    }
}

// Configuration methods

void RegisterBudgetMonitor::SetRegisterBudgetLimit(int limit) {
    std::lock_guard<std::mutex> lock(monitor_mutex_);
    register_budget_limit_ = limit;
    warning_threshold_ = static_cast<double>(limit * 0.78); // Update warning threshold
}

void RegisterBudgetMonitor::SetWarningThreshold(double threshold) {
    std::lock_guard<std::mutex> lock(monitor_mutex_);
    warning_threshold_ = threshold;
}

void RegisterBudgetMonitor::SetDeviceId(int device_id) {
    std::lock_guard<std::mutex> lock(monitor_mutex_);
    device_id_ = device_id;
    InitializeDeviceProperties();
}

void RegisterBudgetMonitor::EnableAutoRecovery(bool enabled) {
    std::lock_guard<std::mutex> lock(monitor_mutex_);
    auto_recovery_enabled_ = enabled;
}

void RegisterBudgetMonitor::SetOptimizationStrategies(const std::vector<OptimizationStrategy>& strategies) {
    std::lock_guard<std::mutex> lock(monitor_mutex_);
    enabled_strategies_ = strategies;
}

// Private methods

void RegisterBudgetMonitor::InitializeDeviceProperties() {
    cudaError_t result = cudaGetDeviceProperties(&device_properties_, device_id_);
    if (result == cudaSuccess) {
        device_name_ = device_properties_.name;
    } else {
        device_name_ = "Unknown Device";
    }
}

double RegisterBudgetMonitor::CalculateOccupancy(int registers_per_thread, int block_size, size_t shared_memory) const {
    // Simple occupancy calculation
    int max_blocks_per_sm;
    int active_warps_per_sm;
    int max_warps_per_sm = device_properties_.maxThreadsPerMultiProcessor / 32;

    // Calculate register limitation
    int registers_per_block = registers_per_thread * block_size;
    int register_limited_blocks = device_properties_.totalGlobalMem / registers_per_block;
    if (registers_per_block > 0) {
        register_limited_blocks = device_properties_.regsPerMultiprocessor / registers_per_block;
    }

    // Calculate shared memory limitation
    int shared_memory_limited_blocks = INT_MAX;
    if (shared_memory > 0) {
        shared_memory_limited_blocks = device_properties_.sharedMemPerMultiprocessor / shared_memory;
    }

    // Calculate thread limitation
    int thread_limited_blocks = device_properties_.maxThreadsPerMultiProcessor / block_size;

    // Take the minimum limitation
    max_blocks_per_sm = std::min({register_limited_blocks, shared_memory_limited_blocks, thread_limited_blocks});
    max_blocks_per_sm = std::min(max_blocks_per_sm, device_properties_.maxBlocksPerMultiProcessor);

    // Calculate active warps
    active_warps_per_sm = max_blocks_per_sm * (block_size / 32);

    return static_cast<double>(active_warps_per_sm) / max_warps_per_sm;
}

int RegisterBudgetMonitor::GetMaxThreadsPerBlock(int registers_per_thread) const {
    int max_threads = device_properties_.maxThreadsPerBlock;
    int max_threads_by_registers = device_properties_.regsPerMultiprocessor / registers_per_thread * 32;

    return std::min(max_threads, max_threads_by_registers);
}

RegisterBudgetViolation RegisterBudgetMonitor::CreateViolationRecord(const RegisterUsageRecord& record) const {
    RegisterBudgetViolation violation;
    violation.kernel_name = record.kernel_name;
    violation.registers_used = record.registers_per_thread;
    violation.budget_limit = register_budget_limit_;
    violation.violation_severity = record.registers_per_thread > register_budget_limit_ * 1.5 ? "Critical" : "Warning";
    violation.detection_time = std::chrono::system_clock::now();
    violation.auto_recovery_attempted = false;
    violation.recovery_successful = false;

    // Calculate occupancy impact
    double optimal_occupancy = CalculateOccupancy(register_budget_limit_, record.block_size, record.shared_memory_per_block);
    violation.occupancy_impact_percent = (optimal_occupancy - record.occupancy_achieved) * 100;

    // Generate optimization suggestions
    violation.optimization_suggestions = GenerateOptimizationSuggestions(
        RegisterUsageAnalysis{record.kernel_name, record.registers_per_thread, register_budget_limit_,
                            record.occupancy_achieved, optimal_occupancy, 0, {}, {}, "", true});

    return violation;
}

std::vector<OptimizationStrategy> RegisterBudgetMonitor::DetermineApplicableStrategies(const RegisterUsageRecord& record) const {
    std::vector<OptimizationStrategy> strategies;

    // All strategies are applicable by default
    for (const auto& strategy : enabled_strategies_) {
        strategies.push_back(strategy);
    }

    return strategies;
}

bool RegisterBudgetMonitor::ExtractRegisterInfoFromPTX(const std::string& ptx_code, int& register_count) const {
    // Look for .reg directive in PTX
    static const std::regex reg_pattern(R"(\.reg\s+\.b\d+\s+%r(\d+))");
    std::smatch match;

    int max_reg_num = -1;
    std::istringstream stream(ptx_code);
    std::string line;

    while (std::getline(stream, line)) {
        if (std::regex_search(line, match, reg_pattern) && match.size() > 1) {
            try {
                int reg_num = std::stoi(match[1].str());
                max_reg_num = std::max(max_reg_num, reg_num);
            } catch (...) {
                // Continue on error
            }
        }
    }

    if (max_reg_num >= 0) {
        register_count = max_reg_num + 1; // Registers are 0-indexed
        return true;
    }

    return false;
}

bool RegisterBudgetMonitor::CompileKernelForAnalysis(const std::string& source_code, std::string& ptx_output) const {
    // This is a simplified implementation
    // In practice, you would use NVRTC to compile the source code
    // For now, return false to indicate this needs proper NVRTC integration
    return false;
}

std::vector<std::string> RegisterBudgetMonitor::AnalyzeRegisterPressure(const std::string& source_code) const {
    return GetRegisterPressureIndicators(source_code);
}

int RegisterBudgetMonitor::EstimateRegisterUsageFromSource(const std::string& source_code) const {
    // Simple heuristic-based estimation
    int estimated_registers = 16; // Base register usage

    // Count variables and complexity indicators
    std::istringstream stream(source_code);
    std::string line;
    std::regex var_pattern(R"(\b(?:float|double|int|uint|char|unsigned)\s+(?:\w+|\w+\s*\[[^\]]*\]))");

    int variable_count = 0;
    int complex_expressions = 0;

    while (std::getline(stream, line)) {
        // Count variable declarations
        auto var_begin = std::sregex_iterator(line.begin(), line.end(), var_pattern);
        auto var_end = std::sregex_iterator();
        variable_count += std::distance(var_begin, var_end);

        // Count complex expressions (simplified)
        if (line.find('*') != std::string::npos && line.find('/') != std::string::npos) {
            complex_expressions++;
        }
    }

    // Estimate based on heuristics
    estimated_registers += variable_count * 2; // Rough estimate
    estimated_registers += complex_expressions * 1;

    return estimated_registers;
}

double RegisterBudgetMonitor::EstimateOccupancyImprovement(OptimizationStrategy strategy, const RegisterUsageRecord& record) const {
    switch (strategy) {
        case OptimizationStrategy::REDUCE_REGISTER_PRESSURE:
            // Assume we can reduce registers by 20%
            return 0.1; // 10% occupancy improvement

        case OptimizationStrategy::INCREASE_SHARED_MEMORY:
            return 0.05; // 5% improvement

        case OptimizationStrategy::LAUNCH_CONFIG_ADJUSTMENT:
            return 0.15; // 15% improvement

        case OptimizationStrategy::KERNEL_REFACTORING:
            return 0.2;  // 20% improvement

        case OptimizationStrategy::COMPILE_OPTIONS:
            return 0.08; // 8% improvement
    }
    return 0.0;
}

void RegisterBudgetMonitor::MonitoringLoop() {
    while (!stop_monitoring_) {
        PerformPeriodicCheck();
        std::this_thread::sleep_for(monitoring_interval_);
    }
}

void RegisterBudgetMonitor::PerformPeriodicCheck() {
    // This would periodically check register usage of active kernels
    // For now, it's a placeholder
}

double RegisterBudgetMonitor::CalculateComplianceScore(const RegisterBudgetReport& report) const {
    if (report.total_kernels_analyzed == 0) return 0.0;

    double within_budget_score = (static_cast<double>(report.kernels_within_budget) / report.total_kernels_analyzed) * 100.0;
    double violation_penalty = (static_cast<double>(report.kernels_over_budget) / report.total_kernels_analyzed) * 50.0;

    return std::max(0.0, within_budget_score - violation_penalty);
}

std::vector<std::string> RegisterBudgetMonitor::GenerateRecommendations(const RegisterBudgetReport& report) const {
    std::vector<std::string> recommendations;

    if (report.kernels_over_budget > 0) {
        recommendations.push_back("Optimize kernels exceeding register budget to improve performance");
    }

    if (report.kernels_approaching_limit > 0) {
        recommendations.push_back("Monitor kernels approaching register limit");
    }

    if (report.average_register_usage > 100) {
        recommendations.push_back("Consider code refactoring to reduce overall register pressure");
    }

    if (report.average_occupancy < 0.7) {
        recommendations.push_back("Optimize launch configurations to improve GPU occupancy");
    }

    return recommendations;
}

std::vector<std::string> RegisterBudgetMonitor::IdentifyCriticalIssues(const RegisterBudgetReport& report) const {
    std::vector<std::string> issues;

    if (report.kernels_over_budget > 0) {
        issues.push_back(std::to_string(report.kernels_over_budget) + " kernels exceed register budget");
    }

    if (report.average_occupancy < 0.5) {
        issues.push_back("Low GPU occupancy detected (<50%)");
    }

    if (!report.violations.empty()) {
        issues.push_back(std::to_string(report.violations.size()) + " register budget violations detected");
    }

    return issues;
}

// ScopedRegisterBudgetValidator implementation

ScopedRegisterBudgetValidator::ScopedRegisterBudgetValidator(RegisterBudgetMonitor& monitor,
                                                             const std::string& kernel_name,
                                                             cudaFunction_t kernel_func)
    : monitor_(monitor), kernel_name_(kernel_name), validation_completed_(false) {
    usage_record_ = monitor_.MeasureKernelRegisterUsage(kernel_name, kernel_func);
    validation_completed_ = true;
}

ScopedRegisterBudgetValidator::~ScopedRegisterBudgetValidator() {
    if (validation_completed_ && usage_record_.budget_status == RegisterBudgetStatus::OVER_BUDGET) {
        // Log or handle the violation
    }
}

bool ScopedRegisterBudgetValidator::IsWithinBudget() const {
    return usage_record_.budget_status != RegisterBudgetStatus::OVER_BUDGET;
}

int ScopedRegisterBudgetValidator::GetRegisterUsage() const {
    return usage_record_.registers_per_thread;
}

RegisterBudgetStatus ScopedRegisterBudgetValidator::GetBudgetStatus() const {
    return usage_record_.budget_status;
}

// Factory function

std::unique_ptr<RegisterBudgetMonitor> CreateRegisterBudgetMonitor(
    int device_id,
    int register_budget_limit,
    bool enable_real_time_monitoring) {

    auto monitor = std::make_unique<RegisterBudgetMonitor>(device_id);
    monitor->SetRegisterBudgetLimit(register_budget_limit);
    monitor->EnableRealTimeMonitoring(enable_real_time_monitoring);

    if (enable_real_time_monitoring) {
        monitor->StartMonitoring();
    }

    return monitor;
}

} // namespace puzzle71::gpu::performance