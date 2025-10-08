#pragma once

#include <vector>
#include <memory>
#include <string>
#include <cuda_runtime.h>
#include <chrono>
#include <map>
#include <functional>

namespace puzzle71::gpu::performance {

enum class ErrorType {
    None = 0,
    CudaError,
    MemoryError,
    KernelError,
    ConfigurationError,
    ValidationError,
    DriverError,
    TimeoutError,
    ResourceError
};

enum class ErrorSeverity {
    Info,
    Warning,
    Error,
    Critical
};

struct ErrorContext {
    std::string operation;
    std::string function_name;
    std::string file_name;
    int line_number;
    std::chrono::system_clock::time_point timestamp;
    std::map<std::string, std::string> additional_info;
};

struct ErrorDetails {
    ErrorType type;
    ErrorSeverity severity;
    std::string message;
    std::string error_code;
    ErrorContext context;
    bool recoverable;
    int retry_count;
    std::vector<std::string> recovery_attempts;
    bool recovery_successful;
};

struct RecoveryStrategy {
    std::string name;
    std::function<bool()> action;
    int max_attempts;
    std::chrono::milliseconds cooldown_period;
    double success_rate;
};

class ErrorHandler {
public:
    ErrorHandler();
    virtual ~ErrorHandler();

    // Core error handling methods
    virtual bool HandleCudaError(cudaError_t error, const std::string& context) = 0;
    virtual bool HandleMemoryError(size_t requested_size, const std::string& context) = 0;
    virtual bool HandleKernelError(const std::string& kernel_name, const std::string& error_msg) = 0;
    virtual bool AttemptRecovery(ErrorType error_type) = 0;
    virtual void LogError(ErrorType error_type, const std::string& message) = 0;

    // Enhanced error handling
    virtual bool HandleError(const ErrorDetails& error_details) = 0;
    virtual bool HandleDriverError(int gpu_id, const std::string& driver_error) = 0;
    virtual bool HandleTimeoutError(const std::string& operation, std::chrono::milliseconds timeout) = 0;
    virtual bool HandleResourceError(const std::string& resource_type, const std::string& resource_id) = 0;

    // Recovery strategies
    virtual void RegisterRecoveryStrategy(ErrorType error_type, const RecoveryStrategy& strategy) = 0;
    virtual void UnregisterRecoveryStrategy(ErrorType error_type, const std::string& strategy_name) = 0;
    virtual std::vector<RecoveryStrategy> GetAvailableStrategies(ErrorType error_type) const = 0;

    // Error history and statistics
    virtual std::vector<ErrorDetails> GetErrorHistory(ErrorType filter_type = ErrorType::None) const = 0;
    virtual std::map<ErrorType, int> GetErrorCounts() const = 0;
    virtual double GetRecoverySuccessRate(ErrorType error_type) const = 0;
    virtual void ClearErrorHistory() = 0;

    // Configuration
    virtual void SetErrorCallback(std::function<void(const ErrorDetails&)> callback) = 0;
    virtual void SetMaxRetryAttempts(int max_attempts) = 0;
    virtual void SetRetryCooldown(std::chrono::milliseconds cooldown) = 0;
    virtual void EnableAutoRecovery(bool enable) = 0;

    // Diagnostics
    virtual bool IsInRecoveryMode() const = 0;
    virtual std::string GetLastErrorMessage() const = 0;
    virtual ErrorType GetLastErrorType() const = 0;
    virtual bool CanHandleError(ErrorType error_type) const = 0;

    // Factory method
    static std::unique_ptr<ErrorHandler> Create();
};

// Concrete implementation with comprehensive error handling
class CudaErrorHandler : public ErrorHandler {
public:
    CudaErrorHandler();
    virtual ~CudaErrorHandler();

    // Core error handling
    bool HandleCudaError(cudaError_t error, const std::string& context) override;
    bool HandleMemoryError(size_t requested_size, const std::string& context) override;
    bool HandleKernelError(const std::string& kernel_name, const std::string& error_msg) override;
    bool AttemptRecovery(ErrorType error_type) override;
    void LogError(ErrorType error_type, const std::string& message) override;

    // Enhanced error handling
    bool HandleError(const ErrorDetails& error_details) override;
    bool HandleDriverError(int gpu_id, const std::string& driver_error) override;
    bool HandleTimeoutError(const std::string& operation, std::chrono::milliseconds timeout) override;
    bool HandleResourceError(const std::string& resource_type, const std::string& resource_id) override;

    // Recovery strategies
    void RegisterRecoveryStrategy(ErrorType error_type, const RecoveryStrategy& strategy) override;
    void UnregisterRecoveryStrategy(ErrorType error_type, const std::string& strategy_name) override;
    std::vector<RecoveryStrategy> GetAvailableStrategies(ErrorType error_type) const override;

    // Error history and statistics
    std::vector<ErrorDetails> GetErrorHistory(ErrorType filter_type = ErrorType::None) const override;
    std::map<ErrorType, int> GetErrorCounts() const override;
    double GetRecoverySuccessRate(ErrorType error_type) const override;
    void ClearErrorHistory() override;

    // Configuration
    void SetErrorCallback(std::function<void(const ErrorDetails&)> callback) override;
    void SetMaxRetryAttempts(int max_attempts) override;
    void SetRetryCooldown(std::chrono::milliseconds cooldown) override;
    void EnableAutoRecovery(bool enable) override;

    // Diagnostics
    bool IsInRecoveryMode() const override;
    std::string GetLastErrorMessage() const override;
    ErrorType GetLastErrorType() const override;
    bool CanHandleError(ErrorType error_type) const override;

private:
    // Error handling state
    std::vector<ErrorDetails> error_history_;
    std::map<ErrorType, std::vector<RecoveryStrategy>> recovery_strategies_;
    std::function<void(const ErrorDetails&)> error_callback_;
    ErrorDetails last_error_;
    bool recovery_mode_active_ = false;
    bool auto_recovery_enabled_ = true;
    int max_retry_attempts_ = 3;
    std::chrono::milliseconds retry_cooldown_{1000};  // 1 second cooldown
    std::chrono::steady_clock::time_point last_recovery_attempt_;

    // Error statistics
    std::map<ErrorType, int> error_counts_;
    std::map<ErrorType, int> recovery_success_counts_;
    std::map<ErrorType, int> recovery_attempt_counts_;

    // Helper methods
    void InitializeDefaultRecoveryStrategies();
    void AddErrorToHistory(const ErrorDetails& error_details);
    void UpdateErrorStatistics(const ErrorDetails& error_details);
    std::string ErrorTypeToString(ErrorType type) const;
    std::string SeverityToString(ErrorSeverity severity) const;
    ErrorSeverity ClassifyErrorSeverity(ErrorType type, const std::string& message) const;

    // Recovery strategy implementations
    bool RecoverFromCudaError();
    bool RecoverFromMemoryError();
    bool RecoverFromKernelError();
    bool RecoverFromDriverError();
    bool RecoverFromTimeoutError();
    bool RecoverFromResourceError();

    // GPU-specific recovery
    bool ResetGpuState(int gpu_id);
    bool ClearGpuMemory(int gpu_id);
    bool RestartGpuDriver(int gpu_id);
    bool FallbackToConservativeConfiguration(int gpu_id);

    // Context creation
    ErrorContext CreateErrorContext(const std::string& operation,
                                   const std::string& function_name,
                                   const std::string& file_name,
                                   int line_number) const;
};

// Utility macros for error handling
#define HANDLE_CUDA_ERROR(error, context) \
    do { \
        if (error != cudaSuccess) { \
            auto handler = puzzle71::gpu::performance::CudaErrorHandler(); \
            handler.HandleCudaError(error, context); \
        } \
    } while (0)

#define LOG_ERROR(type, message) \
    do { \
        auto handler = puzzle71::gpu::performance::CudaErrorHandler(); \
        handler.LogError(type, message); \
    } while (0)

// RAII Error Context for automatic error reporting
class ErrorContextGuard {
public:
    ErrorContextGuard(ErrorHandler& handler, const std::string& operation,
                      const std::string& function, const std::string& file, int line)
        : handler_(handler), operation_(operation), function_(function),
          file_(file), line_(line) {}

    ~ErrorContextGuard() {
        // Automatically log any unhandled errors on scope exit
        // This would integrate with a more sophisticated error tracking system
    }

    void ReportError(ErrorType type, const std::string& message, ErrorSeverity severity) {
        ErrorDetails details;
        details.type = type;
        details.severity = severity;
        details.message = message;
        // Create error context directly since method is private
        details.context.operation = operation_;
        details.context.function_name = function_;
        details.context.file_name = file_;
        details.context.line_number = line_;
        details.context.timestamp = std::chrono::system_clock::now();
        details.recoverable = handler_.CanHandleError(type);
        handler_.HandleError(details);
    }

private:
    ErrorHandler& handler_;
    std::string operation_;
    std::string function_;
    std::string file_;
    int line_;
};

} // namespace puzzle71::gpu::performance