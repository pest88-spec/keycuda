// T015: Integration Error Handling and Recovery Mechanisms
// Comprehensive error handling with recovery strategies for integration operations

#pragma once

#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <chrono>
#include <nlohmann/json.hpp>
#include <filesystem>

namespace integration {
namespace error {

struct RecoveryResult {
    bool success = false;
    std::string message;
    nlohmann::json additional_data;
};

struct RecoveryContext {
    int retry_count = 0;
    std::string error_message;
    nlohmann::json custom_data;
    std::map<std::string, std::string> parameters;
};

struct RecoveryStrategy {
    bool retry_on_failure = true;
    std::vector<std::string> actions;
};

class ErrorHandler {
public:
    ErrorHandler();

    // Core error handling with recovery
    bool execute_with_recovery(const std::string& operation,
                               std::function<bool()> func,
                               RecoveryContext& context);

    // Configuration
    void set_max_retries(int max_retries) { max_retries_ = max_retries; }
    void set_retry_delay(int delay_ms) { retry_delay_ms_ = delay_ms; }

    // Recovery strategy management
    void set_recovery_strategy(const std::string& operation, const RecoveryStrategy& strategy) {
        recovery_strategies_[operation] = strategy;
    }

private:
    bool should_retry(const std::string& operation,
                      const RecoveryContext& context,
                      const std::string& error);

    RecoveryResult apply_recovery_strategy(const std::string& operation,
                                           RecoveryContext& context,
                                           int attempt);

    RecoveryResult apply_default_recovery(RecoveryContext& context, int attempt);
    RecoveryResult execute_recovery_action(const std::string& action, RecoveryContext& context);

    // Specific recovery actions
    RecoveryResult cleanup_temporary_files(RecoveryContext& context);
    RecoveryResult reset_operation_state(RecoveryContext& context);
    RecoveryResult verify_environment(RecoveryContext& context);
    RecoveryResult restore_from_backup(RecoveryContext& context);
    RecoveryResult retry_with_different_params(RecoveryContext& context);
    RecoveryResult check_disk_space(RecoveryContext& context);
    RecoveryResult validate_permissions(RecoveryContext& context);

    // Logging
    void log_error(const std::string& operation, int attempt, const std::string& error);
    void log_recovery(const std::string& operation, int attempt, bool success);
    void log_recovery_failure(const std::string& operation, int attempt, const std::string& error);
    void log_final_failure(const std::string& operation, int attempts, const std::string& error);

    // Setup
    void setup_recovery_strategies();

    // System utilities
    static pid_t getpid();

    int max_retries_;
    int retry_delay_ms_;
    std::map<std::string, RecoveryStrategy> recovery_strategies_;
};

// Global accessor
ErrorHandler& get_error_handler();

// Convenience macro for error handling with recovery
#define EXECUTE_WITH_RECOVERY(operation, context, func) \
    integration::error::get_error_handler().execute_with_recovery(operation, func, context)

} // namespace error
} // namespace integration