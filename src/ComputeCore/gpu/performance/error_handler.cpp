#include "ComputeCore/gpu/performance/error_handler.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <iomanip>

namespace puzzle71::gpu::performance {

// Factory method implementation
std::unique_ptr<ErrorHandler> ErrorHandler::Create() {
    return std::make_unique<CudaErrorHandler>();
}

// Base class
ErrorHandler::ErrorHandler() = default;
ErrorHandler::~ErrorHandler() = default;

// CudaErrorHandler implementation
CudaErrorHandler::CudaErrorHandler() {
    InitializeDefaultRecoveryStrategies();
}

CudaErrorHandler::~CudaErrorHandler() = default;

bool CudaErrorHandler::HandleCudaError(cudaError_t error, const std::string& context) {
    ErrorDetails details;
    details.type = ErrorType::CudaError;
    details.error_code = std::to_string(static_cast<int>(error));
    details.message = std::string(cudaGetErrorString(error));
    details.context.operation = context;
    details.context.timestamp = std::chrono::system_clock::now();
    details.recoverable = CanHandleError(ErrorType::CudaError);
    details.retry_count = 0;
    details.recovery_successful = false;

    return HandleError(details);
}

bool CudaErrorHandler::HandleMemoryError(size_t requested_size, const std::string& context) {
    ErrorDetails details;
    details.type = ErrorType::MemoryError;
    details.error_code = "OUT_OF_MEMORY";
    details.message = "Failed to allocate " + std::to_string(requested_size) + " bytes";
    details.context.operation = context;
    details.context.timestamp = std::chrono::system_clock::now();
    details.context.additional_info["requested_size"] = std::to_string(requested_size);
    details.recoverable = CanHandleError(ErrorType::MemoryError);
    details.retry_count = 0;
    details.recovery_successful = false;

    return HandleError(details);
}

bool CudaErrorHandler::HandleKernelError(const std::string& kernel_name, const std::string& error_msg) {
    ErrorDetails details;
    details.type = ErrorType::KernelError;
    details.error_code = "KERNEL_ERROR";
    details.message = "Kernel '" + kernel_name + "' failed: " + error_msg;
    details.context.operation = "Kernel Execution";
    details.context.timestamp = std::chrono::system_clock::now();
    details.context.additional_info["kernel_name"] = kernel_name;
    details.recoverable = CanHandleError(ErrorType::KernelError);
    details.retry_count = 0;
    details.recovery_successful = false;

    return HandleError(details);
}

bool CudaErrorHandler::AttemptRecovery(ErrorType error_type) {
    recovery_mode_active_ = true;

    // Check cooldown period
    auto now = std::chrono::steady_clock::now();
    auto time_since_last = now - last_recovery_attempt_;
    if (time_since_last < retry_cooldown_) {
        return false;
    }

    last_recovery_attempt_ = now;
    recovery_attempt_counts_[error_type]++;

    // Try available recovery strategies
    auto strategies = GetAvailableStrategies(error_type);
    for (auto& strategy : strategies) {
        if (strategy.action) {
            // Note: This would need to be tracked more carefully in a real implementation

            if (strategy.action()) {
                recovery_success_counts_[error_type]++;
                recovery_mode_active_ = false;
                return true;
            }
        }
    }

    recovery_mode_active_ = false;
    return false;
}

void CudaErrorHandler::LogError(ErrorType error_type, const std::string& message) {
    ErrorDetails details;
    details.type = error_type;
    details.message = message;
    details.context.timestamp = std::chrono::system_clock::now();
    details.severity = ClassifyErrorSeverity(error_type, message);
    details.recoverable = CanHandleError(error_type);

    AddErrorToHistory(details);
    UpdateErrorStatistics(details);

    // Call user callback if set
    if (error_callback_) {
        error_callback_(details);
    }

    // Log to console
    std::cerr << "[" << SeverityToString(details.severity) << "] "
              << ErrorTypeToString(details.type) << ": " << message << std::endl;
}

bool CudaErrorHandler::HandleError(const ErrorDetails& error_details) {
    last_error_ = error_details;

    // Add to history and update statistics
    AddErrorToHistory(error_details);
    UpdateErrorStatistics(error_details);

    // Log the error
    LogError(error_details.type, error_details.message);

    // Attempt auto-recovery if enabled and error is recoverable
    if (auto_recovery_enabled_ && error_details.recoverable &&
        error_details.retry_count < max_retry_attempts_) {

        ErrorDetails mutable_details = error_details;
        mutable_details.retry_count++;
        mutable_details.recovery_successful = AttemptRecovery(error_details.type);

        if (mutable_details.recovery_successful) {
            std::cout << "Successfully recovered from " << ErrorTypeToString(error_details.type) << std::endl;
            return true;
        }
    }

    return error_details.recovery_successful;
}

bool CudaErrorHandler::HandleDriverError(int gpu_id, const std::string& driver_error) {
    ErrorDetails details;
    details.type = ErrorType::DriverError;
    details.error_code = "DRIVER_ERROR";
    details.message = "GPU " + std::to_string(gpu_id) + " driver error: " + driver_error;
    details.context.operation = "GPU Driver Access";
    details.context.timestamp = std::chrono::system_clock::now();
    details.context.additional_info["gpu_id"] = std::to_string(gpu_id);
    details.recoverable = CanHandleError(ErrorType::DriverError);

    return HandleError(details);
}

bool CudaErrorHandler::HandleTimeoutError(const std::string& operation, std::chrono::milliseconds timeout) {
    ErrorDetails details;
    details.type = ErrorType::TimeoutError;
    details.error_code = "TIMEOUT";
    details.message = "Operation '" + operation + "' timed out after " + std::to_string(timeout.count()) + "ms";
    details.context.operation = operation;
    details.context.timestamp = std::chrono::system_clock::now();
    details.context.additional_info["timeout_ms"] = std::to_string(timeout.count());
    details.recoverable = CanHandleError(ErrorType::TimeoutError);

    return HandleError(details);
}

bool CudaErrorHandler::HandleResourceError(const std::string& resource_type, const std::string& resource_id) {
    ErrorDetails details;
    details.type = ErrorType::ResourceError;
    details.error_code = "RESOURCE_ERROR";
    details.message = "Resource error: " + resource_type + " (" + resource_id + ")";
    details.context.operation = "Resource Access";
    details.context.timestamp = std::chrono::system_clock::now();
    details.context.additional_info["resource_type"] = resource_type;
    details.context.additional_info["resource_id"] = resource_id;
    details.recoverable = CanHandleError(ErrorType::ResourceError);

    return HandleError(details);
}

void CudaErrorHandler::RegisterRecoveryStrategy(ErrorType error_type, const RecoveryStrategy& strategy) {
    recovery_strategies_[error_type].push_back(strategy);
}

void CudaErrorHandler::UnregisterRecoveryStrategy(ErrorType error_type, const std::string& strategy_name) {
    auto& strategies = recovery_strategies_[error_type];
    strategies.erase(
        std::remove_if(strategies.begin(), strategies.end(),
                      [&strategy_name](const RecoveryStrategy& s) { return s.name == strategy_name; }),
        strategies.end()
    );
}

std::vector<RecoveryStrategy> CudaErrorHandler::GetAvailableStrategies(ErrorType error_type) const {
    auto it = recovery_strategies_.find(error_type);
    if (it != recovery_strategies_.end()) {
        return it->second;
    }
    return {};
}

std::vector<ErrorDetails> CudaErrorHandler::GetErrorHistory(ErrorType filter_type) const {
    if (filter_type == ErrorType::None) {
        return error_history_;
    }

    std::vector<ErrorDetails> filtered;
    std::copy_if(error_history_.begin(), error_history_.end(),
                  std::back_inserter(filtered),
                  [filter_type](const ErrorDetails& details) {
                      return details.type == filter_type;
                  });
    return filtered;
}

std::map<ErrorType, int> CudaErrorHandler::GetErrorCounts() const {
    return error_counts_;
}

double CudaErrorHandler::GetRecoverySuccessRate(ErrorType error_type) const {
    auto attempts_it = recovery_attempt_counts_.find(error_type);
    auto successes_it = recovery_success_counts_.find(error_type);

    if (attempts_it == recovery_attempt_counts_.end() || attempts_it->second == 0) {
        return 0.0;
    }

    int attempts = attempts_it->second;
    int successes = (successes_it != recovery_success_counts_.end()) ? successes_it->second : 0;

    return static_cast<double>(successes) / attempts * 100.0;
}

void CudaErrorHandler::ClearErrorHistory() {
    error_history_.clear();
    error_counts_.clear();
    recovery_success_counts_.clear();
    recovery_attempt_counts_.clear();
}

void CudaErrorHandler::SetErrorCallback(std::function<void(const ErrorDetails&)> callback) {
    error_callback_ = callback;
}

void CudaErrorHandler::SetMaxRetryAttempts(int max_attempts) {
    max_retry_attempts_ = std::max(1, max_attempts);
}

void CudaErrorHandler::SetRetryCooldown(std::chrono::milliseconds cooldown) {
    retry_cooldown_ = cooldown;
}

void CudaErrorHandler::EnableAutoRecovery(bool enable) {
    auto_recovery_enabled_ = enable;
}

bool CudaErrorHandler::IsInRecoveryMode() const {
    return recovery_mode_active_;
}

std::string CudaErrorHandler::GetLastErrorMessage() const {
    return last_error_.message;
}

ErrorType CudaErrorHandler::GetLastErrorType() const {
    return last_error_.type;
}

bool CudaErrorHandler::CanHandleError(ErrorType error_type) const {
    // Most error types are recoverable with proper strategies
    switch (error_type) {
        case ErrorType::CudaError:
        case ErrorType::MemoryError:
        case ErrorType::KernelError:
        case ErrorType::TimeoutError:
        case ErrorType::ResourceError:
            return true;
        case ErrorType::DriverError:
        case ErrorType::ConfigurationError:
        case ErrorType::ValidationError:
            return false;  // These typically require manual intervention
        default:
            return false;
    }
}

// Private helper methods
void CudaErrorHandler::InitializeDefaultRecoveryStrategies() {
    // CUDA error recovery strategies
    RegisterRecoveryStrategy(ErrorType::CudaError, {
        "Reset CUDA Device",
        [this]() { return RecoverFromCudaError(); },
        3,
        std::chrono::milliseconds(5000),
        0.7
    });

    // Memory error recovery strategies
    RegisterRecoveryStrategy(ErrorType::MemoryError, {
        "Clear GPU Memory",
        [this]() { return RecoverFromMemoryError(); },
        5,
        std::chrono::milliseconds(1000),
        0.8
    });

    // Kernel error recovery strategies
    RegisterRecoveryStrategy(ErrorType::KernelError, {
        "Restart Kernel Execution",
        [this]() { return RecoverFromKernelError(); },
        3,
        std::chrono::milliseconds(2000),
        0.6
    });

    // Timeout error recovery strategies
    RegisterRecoveryStrategy(ErrorType::TimeoutError, {
        "Reduce Workload",
        [this]() { return RecoverFromTimeoutError(); },
        2,
        std::chrono::milliseconds(1000),
        0.5
    });
}

void CudaErrorHandler::AddErrorToHistory(const ErrorDetails& error_details) {
    // Keep only the last 1000 errors
    if (error_history_.size() >= 1000) {
        error_history_.erase(error_history_.begin());
    }
    error_history_.push_back(error_details);
}

void CudaErrorHandler::UpdateErrorStatistics(const ErrorDetails& error_details) {
    error_counts_[error_details.type]++;
}

std::string CudaErrorHandler::ErrorTypeToString(ErrorType type) const {
    switch (type) {
        case ErrorType::None: return "None";
        case ErrorType::CudaError: return "CUDA Error";
        case ErrorType::MemoryError: return "Memory Error";
        case ErrorType::KernelError: return "Kernel Error";
        case ErrorType::ConfigurationError: return "Configuration Error";
        case ErrorType::ValidationError: return "Validation Error";
        case ErrorType::DriverError: return "Driver Error";
        case ErrorType::TimeoutError: return "Timeout Error";
        case ErrorType::ResourceError: return "Resource Error";
        default: return "Unknown";
    }
}

std::string CudaErrorHandler::SeverityToString(ErrorSeverity severity) const {
    switch (severity) {
        case ErrorSeverity::Info: return "INFO";
        case ErrorSeverity::Warning: return "WARNING";
        case ErrorSeverity::Error: return "ERROR";
        case ErrorSeverity::Critical: return "CRITICAL";
        default: return "UNKNOWN";
    }
}

ErrorSeverity CudaErrorHandler::ClassifyErrorSeverity(ErrorType type, const std::string& message) const {
    switch (type) {
        case ErrorType::MemoryError:
            return ErrorSeverity::Critical;
        case ErrorType::DriverError:
            return ErrorSeverity::Critical;
        case ErrorType::KernelError:
            return ErrorSeverity::Error;
        case ErrorType::CudaError:
            return message.find("out of memory") != std::string::npos ?
                   ErrorSeverity::Critical : ErrorSeverity::Error;
        case ErrorType::TimeoutError:
            return ErrorSeverity::Warning;
        case ErrorType::ConfigurationError:
        case ErrorType::ValidationError:
            return ErrorSeverity::Error;
        case ErrorType::ResourceError:
            return ErrorSeverity::Warning;
        default:
            return ErrorSeverity::Info;
    }
}

// Recovery strategy implementations
bool CudaErrorHandler::RecoverFromCudaError() {
    try {
        // Reset CUDA device
        int device;
        cudaError_t err = cudaGetDevice(&device);
        if (err == cudaSuccess) {
            err = cudaDeviceReset();
            if (err == cudaSuccess) {
                err = cudaSetDevice(device);
                if (err == cudaSuccess) {
                    return true;
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "CUDA recovery failed: " << e.what() << std::endl;
    }
    return false;
}

bool CudaErrorHandler::RecoverFromMemoryError() {
    try {
        // Free memory and reset device
        cudaDeviceReset();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Memory recovery failed: " << e.what() << std::endl;
        return false;
    }
}

bool CudaErrorHandler::RecoverFromKernelError() {
    try {
        // Synchronize and reset
        cudaDeviceSynchronize();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Kernel recovery failed: " << e.what() << std::endl;
        return false;
    }
}

bool CudaErrorHandler::RecoverFromDriverError() {
    // Driver errors typically require manual intervention
    return false;
}

bool CudaErrorHandler::RecoverFromTimeoutError() {
    try {
        // Reduce workload or reset execution
        cudaDeviceSynchronize();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Timeout recovery failed: " << e.what() << std::endl;
        return false;
    }
}

bool CudaErrorHandler::RecoverFromResourceError() {
    try {
        // Clear CUDA cache and reset
        cudaDeviceReset();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Resource recovery failed: " << e.what() << std::endl;
        return false;
    }
}

// GPU-specific recovery methods
bool CudaErrorHandler::ResetGpuState(int gpu_id) {
    try {
        cudaSetDevice(gpu_id);
        cudaDeviceReset();
        return true;
    } catch (...) {
        return false;
    }
}

bool CudaErrorHandler::ClearGpuMemory(int gpu_id) {
    try {
        cudaSetDevice(gpu_id);
        cudaDeviceSynchronize();
        cudaDeviceReset();
        return true;
    } catch (...) {
        return false;
    }
}

bool CudaErrorHandler::RestartGpuDriver(int gpu_id) {
    // This would require system-level privileges
    // Return false as this typically requires manual intervention
    return false;
}

bool CudaErrorHandler::FallbackToConservativeConfiguration(int gpu_id) {
    try {
        cudaSetDevice(gpu_id);
        // Set conservative configuration
        cudaDeviceSetLimit(cudaLimitMallocHeapSize, 64 * 1024 * 1024);  // 64MB heap
        return true;
    } catch (...) {
        return false;
    }
}

ErrorContext CudaErrorHandler::CreateErrorContext(const std::string& operation,
                                                 const std::string& function_name,
                                                 const std::string& file_name,
                                                 int line_number) const {
    ErrorContext context;
    context.operation = operation;
    context.function_name = function_name;
    context.file_name = file_name;
    context.line_number = line_number;
    context.timestamp = std::chrono::system_clock::now();
    return context;
}

} // namespace puzzle71::gpu::performance