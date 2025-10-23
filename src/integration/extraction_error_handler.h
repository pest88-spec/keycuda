#pragma once

/**
 * @file extraction_error_handler.h
 * @brief Error handling and fallback mechanisms for extraction failures
 *
 * T028: Error handling for extraction failures and fallback mechanisms
 *
 * This header defines the error handling system that provides robust
 * extraction operations with fallback mechanisms, retry logic, and
 * recovery strategies for third-party library extraction failures.
 *
 * @author T028 Implementation Team
 * @date 2025-10-22
 */

#include <string>
#include <vector>
#include <chrono>
#include <memory>
#include <functional>
#include <set>
#include <filesystem>
#include <atomic>

namespace integration {
namespace extraction {

/**
 * @brief Extraction error types
 */
enum class ErrorType {
    FILE_NOT_FOUND,
    PERMISSION_DENIED,
    CHECKSUM_MISMATCH,
    NETWORK_ERROR,
    PARSE_ERROR,
    BUILD_ERROR,
    VALIDATION_ERROR,
    DEPENDENCY_ERROR,
    CONFIGURATION_ERROR,
    TIMEOUT_ERROR,
    UNKNOWN_ERROR
};

/**
 * @brief Extraction error information
 */
class ExtractionError {
public:
    ExtractionError(ErrorType type, const std::string& message, const std::string& context = "");

    ErrorType get_type() const { return type_; }
    const std::string& get_message() const { return message_; }
    const std::string& get_context() const { return context_; }
    std::chrono::system_clock::time_point get_timestamp() const { return timestamp_; }

    std::string type_to_string() const;
    bool is_recoverable() const;

private:
    ErrorType type_;
    std::string message_;
    std::string context_;
    std::chrono::system_clock::time_point timestamp_;
};

/**
 * @brief Retry strategy configuration
 */
class RetryStrategy {
public:
    RetryStrategy(uint32_t max_attempts = 3,
                  std::chrono::milliseconds base_delay = std::chrono::milliseconds(1000));

    bool should_retry(const ExtractionError& error) const;
    std::chrono::milliseconds get_delay() const;
    void increment_attempt();
    void reset();

    uint32_t get_current_attempt() const { return current_attempt_; }
    uint32_t get_max_attempts() const { return max_attempts_; }

private:
    uint32_t max_attempts_;
    std::chrono::milliseconds base_delay_;
    uint32_t current_attempt_;
    double backoff_multiplier_;
};

/**
 * @brief Fallback option types
 */
enum class FallbackType {
    USE_CACHED,
    USE_SYSTEM_PACKAGE,
    MINIMAL_EXTRACTION,
    SKIP_EXTRACTION
};

/**
 * @brief Fallback option configuration
 */
class FallbackOption {
public:
    FallbackOption(FallbackType type, const std::string& name, const std::string& description);

    FallbackType get_type() const { return type_; }
    const std::string& get_name() const { return name_; }
    const std::string& get_description() const { return description_; }

    void set_available(bool available) { available_ = available; }
    bool is_available() const;

    void set_condition(std::function<bool()> condition);
    bool execute(std::function<bool()> action) const;

private:
    FallbackType type_;
    std::string name_;
    std::string description_;
    std::function<bool()> condition_;
    bool available_;
};

/**
 * @brief Extraction error handler interface
 */
class ExtractionErrorHandler {
public:
    ExtractionErrorHandler();
    ~ExtractionErrorHandler();

    /**
     * @brief Extract with fallback support
     * @param library_name Name of the library to extract
     * @param source_path Source path of the library
     * @param target_path Target path for extraction
     * @param extraction_func Extraction function to try
     * @return True if extraction succeeded (primary or fallback)
     */
    bool extract_with_fallback(
        const std::string& library_name,
        const std::string& source_path,
        const std::string& target_path,
        std::function<bool(const std::string&, const std::string&, const std::string&)> extraction_func);

    /**
     * @brief Set project root directory
     * @param root Project root path
     */
    void set_project_root(const std::string& root);

    /**
     * @brief Set optional libraries list
     * @param libraries Set of optional library names
     */
    void set_optional_libraries(const std::set<std::string>& libraries);

    /**
     * @brief Set current library being processed
     * @param library_name Current library name
     */
    void set_current_library(const std::string& library_name);

    /**
     * @brief Enable or disable logging
     * @param enabled Whether to enable logging
     */
    void enable_logging(bool enabled);

    /**
     * @brief Get recorded errors
     * @return Vector of extraction errors
     */
    std::vector<ExtractionError> get_errors() const;

    /**
     * @brief Clear recorded errors
     */
    void clear_errors();

    /**
     * @brief Add fallback option
     * @param option Fallback option to add
     */
    void add_fallback_option(std::shared_ptr<FallbackOption> option) {
        fallback_options_.push_back(option);
    }

protected:
    virtual bool execute_fallback(
        FallbackOption::Type type,
        const std::string& library_name,
        const std::string& source_path,
        const std::string& target_path);

    virtual bool restore_cached_extraction(
        const std::string& library_name,
        const std::string& target_path);

    virtual bool install_system_package(const std::string& library_name);

    virtual bool perform_minimal_extraction(
        const std::string& library_name,
        const std::string& source_path,
        const std::string& target_path);

    virtual bool handle_optional_dependency(const std::string& library_name);

    virtual void cache_extraction(
        const std::string& library_name,
        const std::string& source_path,
        const std::string& target_path);

    virtual void record_error(const ExtractionError& error);

    virtual bool has_cached_extraction() const;
    virtual bool can_use_system_package() const;
    virtual bool is_optional_dependency() const;

    virtual bool execute_command(const std::string& command);

    virtual void log_info(const std::string& message);
    virtual void log_warning(const std::string& message);
    virtual void log_error(const std::string& message);
    virtual void log_success(const std::string& message);

private:
    void initialize_default_fallbacks();

    std::unique_ptr<RetryStrategy> retry_strategy_;
    std::vector<std::shared_ptr<FallbackOption>> fallback_options_;
    std::vector<ExtractionError> errors_;
    std::string project_root_;
    std::set<std::string> optional_libraries_;
    std::string current_library_;
    bool logging_enabled_;
};

// Utility functions
std::unique_ptr<ExtractionErrorHandler> create_error_handler();

/**
 * @brief Extract with error handling and fallback support
 * @param library_name Name of the library
 * @param source_path Source path
 * @param target_path Target path
 * @param extraction_func Extraction function
 * @param handler Optional error handler (creates one if null)
 * @return True if extraction succeeded
 */
bool extract_with_error_handling(
    const std::string& library_name,
    const std::string& source_path,
    const std::string& target_path,
    std::function<bool(const std::string&, const std::string&, const std::string&)> extraction_func,
    ExtractionErrorHandler* handler = nullptr);

} // namespace extraction
} // namespace integration