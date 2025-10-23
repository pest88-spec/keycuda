// T015: Integration Error Handling and Recovery Mechanisms
// Comprehensive error handling with recovery strategies for integration operations

#include "error_handler.h"
#include <sstream>
#include <ctime>
#include <fstream>

namespace integration {
namespace error {

ErrorHandler::ErrorHandler()
    : max_retries_(3)
    , retry_delay_ms_(1000) {
    setup_recovery_strategies();
}

bool ErrorHandler::execute_with_recovery(const std::string& operation,
                                         std::function<bool()> func,
                                         RecoveryContext& context) {
    int attempt = 0;
    std::string last_error;

    while (attempt < max_retries_) {
        attempt++;

        try {
            bool success = func();
            if (success) {
                // Operation succeeded
                if (attempt > 1) {
                    log_recovery(operation, attempt, true);
                }
                return true;
            }
        } catch (const std::exception& e) {
            last_error = e.what();
            log_error(operation, attempt, e.what());
        }

        // Check if we should retry
        if (attempt < max_retries_) {
            if (!should_retry(operation, context, last_error)) {
                break;
            }

            // Apply recovery strategy
            RecoveryResult recovery = apply_recovery_strategy(operation, context, attempt);
            if (!recovery.success) {
                log_recovery_failure(operation, attempt, recovery.message);
                break;
            }

            // Wait before retry
            std::this_thread::sleep_for(std::chrono::milliseconds(retry_delay_ms_ * attempt));
        }
    }

    // All attempts failed
    context.error_message = "Operation failed after " + std::to_string(attempt) +
                           " attempts. Last error: " + last_error;
    log_final_failure(operation, attempt, last_error);

    return false;
}

bool ErrorHandler::should_retry(const std::string& operation,
                               const RecoveryContext& context,
                               const std::string& error) {
    // Don't retry certain types of errors
    if (error.find("permission denied") != std::string::npos ||
        error.find("access denied") != std::string::npos ||
        error.find("invalid configuration") != std::string::npos) {
        return false;
    }

    // Retry network-related and temporary errors
    if (error.find("timeout") != std::string::npos ||
        error.find("connection refused") != std::string::npos ||
        error.find("temporary failure") != std::string::npos ||
        error.find("resource busy") != std::string::npos) {
        return true;
    }

    // Retry based on operation type
    if (recovery_strategies_.find(operation) != recovery_strategies_.end()) {
        return recovery_strategies_[operation].retry_on_failure;
    }

    // Default: retry transient errors
    return true;
}

RecoveryResult ErrorHandler::apply_recovery_strategy(const std::string& operation,
                                                     RecoveryContext& context,
                                                     int attempt) {
    auto it = recovery_strategies_.find(operation);
    if (it == recovery_strategies_.end()) {
        return apply_default_recovery(context, attempt);
    }

    const auto& strategy = it->second;
    RecoveryResult result;

    for (const auto& action : strategy.actions) {
        result = execute_recovery_action(action, context);
        if (!result.success) {
            return result;
        }
    }

    result.success = true;
    result.message = "Recovery actions completed successfully";
    return result;
}

RecoveryResult ErrorHandler::apply_default_recovery(RecoveryContext& context, int attempt) {
    RecoveryResult result;

    // Default recovery actions
    std::vector<std::string> default_actions = {
        "cleanup_temporary_files",
        "reset_state",
        "verify_environment"
    };

    for (const auto& action : default_actions) {
        result = execute_recovery_action(action, context);
        if (!result.success) {
            break;
        }
    }

    return result;
}

RecoveryResult ErrorHandler::execute_recovery_action(const std::string& action,
                                                     RecoveryContext& context) {
    RecoveryResult result;

    if (action == "cleanup_temporary_files") {
        result = cleanup_temporary_files(context);
    } else if (action == "reset_state") {
        result = reset_operation_state(context);
    } else if (action == "verify_environment") {
        result = verify_environment(context);
    } else if (action == "restore_backup") {
        result = restore_from_backup(context);
    } else if (action == "retry_with_different_params") {
        result = retry_with_different_params(context);
    } else if (action == "check_disk_space") {
        result = check_disk_space(context);
    } else if (action == "validate_permissions") {
        result = validate_permissions(context);
    } else {
        result.success = false;
        result.message = "Unknown recovery action: " + action;
    }

    return result;
}

RecoveryResult ErrorHandler::cleanup_temporary_files(RecoveryContext& context) {
    RecoveryResult result;

    try {
        // Clean up common temporary directories
        std::vector<std::string> temp_dirs = {
            "build/tmp",
            "build/.tmp",
            "/tmp/integration-" + std::to_string(getpid())
        };

        int cleaned = 0;
        for (const auto& dir : temp_dirs) {
            if (std::filesystem::exists(dir)) {
                std::filesystem::remove_all(dir);
                cleaned++;
            }
        }

        result.success = true;
        result.message = "Cleaned " + std::to_string(cleaned) + " temporary directories";
    } catch (const std::exception& e) {
        result.success = false;
        result.message = "Cleanup failed: " + std::string(e.what());
    }

    return result;
}

RecoveryResult ErrorHandler::reset_operation_state(RecoveryContext& context) {
    RecoveryResult result;

    // Reset context state
    context.retry_count = 0;
    context.error_message.clear();
    context.custom_data.clear();

    result.success = true;
    result.message = "Operation state reset";

    return result;
}

RecoveryResult ErrorHandler::verify_environment(RecoveryContext& context) {
    RecoveryResult result;

    // Check basic environment requirements
    std::vector<std::string> required_dirs = {
        "src",
        "src/extracted",
        "src/integration",
        "build"
    };

    for (const auto& dir : required_dirs) {
        if (!std::filesystem::exists(dir)) {
            try {
                std::filesystem::create_directories(dir);
            } catch (const std::exception& e) {
                result.success = false;
                result.message = "Cannot create required directory: " + dir + " - " + e.what();
                return result;
            }
        }
    }

    result.success = true;
    result.message = "Environment verified and prepared";

    return result;
}

RecoveryResult ErrorHandler::restore_from_backup(RecoveryContext& context) {
    RecoveryResult result;

    // Check if backup exists
    std::string backup_dir = "build/backup";
    if (!std::filesystem::exists(backup_dir)) {
        result.success = false;
        result.message = "No backup directory found";
        return result;
    }

    // Restore from backup (simplified)
    try {
        // In production, implement actual backup restoration
        result.success = true;
        result.message = "Backup restoration simulated";
    } catch (const std::exception& e) {
        result.success = false;
        result.message = "Backup restoration failed: " + e.what();
    }

    return result;
}

RecoveryResult ErrorHandler::retry_with_different_params(RecoveryContext& context) {
    RecoveryResult result;

    // Modify context parameters for retry
    if (context.custom_data.contains("timeout")) {
        int old_timeout = context.custom_data["timeout"];
        context.custom_data["timeout"] = old_timeout * 2;
    }

    if (context.custom_data.contains("retry_delay")) {
        int old_delay = context.custom_data["retry_delay"];
        context.custom_data["retry_delay"] = old_delay * 2;
    }

    result.success = true;
    result.message = "Parameters adjusted for retry";

    return result;
}

RecoveryResult ErrorHandler::check_disk_space(RecoveryContext& context) {
    RecoveryResult result;

    try {
        auto space = std::filesystem::space(".");
        double free_gb = static_cast<double>(space.free) / (1024.0 * 1024.0 * 1024.0);

        if (free_gb < 1.0) { // Less than 1GB free
            result.success = false;
            result.message = "Insufficient disk space: " + std::to_string(free_gb) + "GB free";
        } else {
            result.success = true;
            result.message = "Disk space OK: " + std::to_string(free_gb) + "GB free";
        }
    } catch (const std::exception& e) {
        result.success = false;
        result.message = "Failed to check disk space: " + e.what();
    }

    return result;
}

RecoveryResult ErrorHandler::validate_permissions(RecoveryContext& context) {
    RecoveryResult result;

    // Check write permissions on key directories
    std::vector<std::string> critical_dirs = {
        "src/extracted",
        "build",
        "build/evidence",
        "build/integration-metrics"
    };

    for (const auto& dir : critical_dirs) {
        if (std::filesystem::exists(dir)) {
            std::ofstream test_file(dir + "/.permission_test");
            if (test_file.is_open()) {
                test_file.close();
                std::filesystem::remove(dir + "/.permission_test");
            } else {
                result.success = false;
                result.message = "No write permission in directory: " + dir;
                return result;
            }
        }
    }

    result.success = true;
    result.message = "Write permissions validated";

    return result;
}

void ErrorHandler::log_error(const std::string& operation, int attempt, const std::string& error) {
    std::ofstream log_file("build/integration-errors.log", std::ios::app);
    if (log_file.is_open()) {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);

        log_file << "[" << std::put_time(std::gmtime(&time_t), "%Y-%m-%d %H:%M:%S") << "] "
                  << "ERROR in " << operation << " (attempt " << attempt << "): " << error << std::endl;
    }
}

void ErrorHandler::log_recovery(const std::string& operation, int attempt, bool success) {
    std::ofstream log_file("build/integration-errors.log", std::ios::app);
    if (log_file.is_open()) {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);

        log_file << "[" << std::put_time(std::gmtime(&time_t), "%Y-%m-%d %H:%M:%S") << "] "
                  << "RECOVERY for " << operation << " (attempt " << attempt << "): "
                  << (success ? "SUCCESS" : "FAILED") << std::endl;
    }
}

void ErrorHandler::log_recovery_failure(const std::string& operation, int attempt, const std::string& error) {
    std::ofstream log_file("build/integration-errors.log", std::ios::app);
    if (log_file.is_open()) {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);

        log_file << "[" << std::put_time(std::gmtime(&time_t), "%Y-%m-%d %H:%M:%S") << "] "
                  << "RECOVERY FAILED for " << operation << " (attempt " << attempt << "): " << error << std::endl;
    }
}

void ErrorHandler::log_final_failure(const std::string& operation, int attempts, const std::string& error) {
    std::ofstream log_file("build/integration-errors.log", std::ios::app);
    if (log_file.is_open()) {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);

        log_file << "[" << std::put_time(std::gmtime(&time_t), "%Y-%m-%d %H:%M:%S") << "] "
                  << "FINAL FAILURE: " << operation << " failed after " << attempts
                  << " attempts. Last error: " << error << std::endl << std::endl;
    }
}

void ErrorHandler::setup_recovery_strategies() {
    // Library extraction recovery
    RecoveryStrategy extraction_strategy;
    extraction_strategy.retry_on_failure = true;
    extraction_strategy.actions = {
        "cleanup_temporary_files",
        "verify_environment",
        "check_disk_space"
    };
    recovery_strategies_["library_extraction"] = extraction_strategy;

    // Attribution verification recovery
    RecoveryStrategy attribution_strategy;
    attribution_strategy.retry_on_failure = true;
    attribution_strategy.actions = {
        "reset_state"
    };
    recovery_strategies_["attribution_verification"] = attribution_strategy;

    // Build integration recovery
    RecoveryStrategy build_strategy;
    build_strategy.retry_on_failure = true;
    build_strategy.actions = {
        "cleanup_temporary_files",
        "validate_permissions",
        "verify_environment"
    };
    recovery_strategies_["build_integration"] = build_strategy;
}

pid_t ErrorHandler::getpid() {
    return ::getpid();
}

// Global instance
static std::unique_ptr<ErrorHandler> g_error_handler;

ErrorHandler& get_error_handler() {
    if (!g_error_handler) {
        g_error_handler = std::make_unique<ErrorHandler>();
    }
    return *g_error_handler;
}

} // namespace error
} // namespace integration