/**
 * @file extraction_error_handler.cpp
 * @brief Error handling and fallback mechanisms for extraction failures
 *
 * T028: Error handling for extraction failures and fallback mechanisms
 *
 * This system provides comprehensive error handling for third-party library
 * extraction operations, including fallback mechanisms, retry logic, and
 * recovery strategies to ensure robust integration processes.
 *
 * @author T028 Implementation Team
 * @date 2025-10-22
 */

#include "extraction_error_handler.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <thread>
#include <chrono>
#include <random>

namespace integration {
namespace extraction {

// ExtractionError implementation
ExtractionError::ExtractionError(Type type, const std::string& message, const std::string& context)
    : type_(type), message_(message), context_(context), timestamp_(std::chrono::system_clock::now()) {
}

std::string ExtractionError::type_to_string(Type type) const {
    switch (type) {
        case Type::FILE_NOT_FOUND: return "FILE_NOT_FOUND";
        case Type::PERMISSION_DENIED: return "PERMISSION_DENIED";
        case Type::CHECKSUM_MISMATCH: return "CHECKSUM_MISMATCH";
        case Type::NETWORK_ERROR: return "NETWORK_ERROR";
        case Type::PARSE_ERROR: return "PARSE_ERROR";
        case Type::BUILD_ERROR: return "BUILD_ERROR";
        case Type::VALIDATION_ERROR: return "VALIDATION_ERROR";
        case Type::DEPENDENCY_ERROR: return "DEPENDENCY_ERROR";
        case Type::CONFIGURATION_ERROR: return "CONFIGURATION_ERROR";
        case Type::TIMEOUT_ERROR: return "TIMEOUT_ERROR";
        case Type::UNKNOWN_ERROR: return "UNKNOWN_ERROR";
    }
    return "UNKNOWN_ERROR";
}

bool ExtractionError::is_recoverable() const {
    switch (type_) {
        case Type::NETWORK_ERROR:
        case Type::TIMEOUT_ERROR:
        case Type::DEPENDENCY_ERROR:
            return true;
        case Type::PERMISSION_DENIED:
        case Type::BUILD_ERROR:
        case Type::VALIDATION_ERROR:
            return false;
        default:
            return true;  // Default to recoverable
    }
}

// RetryStrategy implementation
RetryStrategy::RetryStrategy(uint32_t max_attempts, std::chrono::milliseconds base_delay)
    : max_attempts_(max_attempts)
    , base_delay_(base_delay)
    , current_attempt_(0)
    , backoff_multiplier_(2.0) {
}

bool RetryStrategy::should_retry(const ExtractionError& error) const {
    if (current_attempt_ >= max_attempts_) {
        return false;
    }

    // Don't retry non-recoverable errors
    if (!error.is_recoverable()) {
        return false;
    }

    // Always retry recoverable errors within limit
    return true;
}

std::chrono::milliseconds RetryStrategy::get_delay() const {
    // Exponential backoff with jitter
    auto delay = base_delay_ * std::pow(backoff_multiplier_, current_attempt_);

    // Add random jitter (±25%)
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> dis(0.75, 1.25);
    delay = std::chrono::milliseconds(static_cast<int64_t>(delay.count() * dis(gen)));

    return std::min(delay, std::chrono::minutes(5));  // Cap at 5 minutes
}

void RetryStrategy::increment_attempt() {
    current_attempt_++;
}

void RetryStrategy::reset() {
    current_attempt_ = 0;
}

// FallbackOption implementation
FallbackOption::FallbackOption(FallbackType type, const std::string& name, const std::string& description)
    : type_(type), name_(name), description_(description), available_(true) {
}

void FallbackOption::set_condition(std::function<bool()> condition) {
    condition_ = condition;
}

bool FallbackOption::is_available() const {
    if (!available_) {
        return false;
    }

    if (condition_) {
        return condition_();
    }

    return true;
}

bool FallbackOption::execute(std::function<bool()> action) const {
    if (!is_available()) {
        return false;
    }

    try {
        return action();
    } catch (const std::exception& e) {
        return false;
    }
}

// ExtractionErrorHandler implementation
ExtractionErrorHandler::ExtractionErrorHandler()
    : retry_strategy_(std::make_unique<RetryStrategy>(3, std::chrono::milliseconds(1000)))
    , logging_enabled_(true) {

    // Initialize default fallback options
    initialize_default_fallbacks();
}

ExtractionErrorHandler::~ExtractionErrorHandler() = default;

void ExtractionErrorHandler::initialize_default_fallbacks() {
    // Fallback 1: Use cached extraction
    auto cached_fallback = std::make_shared<FallbackOption>(
        FallbackOption::Type::USE_CACHED,
        "cached_extraction",
        "Use previously cached extraction if available"
    );
    cached_fallback->set_condition([this]() {
        return has_cached_extraction();
    });
    fallback_options_.push_back(cached_fallback);

    // Fallback 2: Use system package manager
    auto package_fallback = std::make_shared<FallbackOption>(
        FallbackOption::Type::USE_SYSTEM_PACKAGE,
        "system_package",
        "Install from system package manager"
    );
    package_fallback->set_condition([this]() {
        return can_use_system_package();
    });
    fallback_options_.push_back(package_fallback);

    // Fallback 3: Use minimal extraction
    auto minimal_fallback = std::make_shared<FallbackOption>(
        FallbackOption::Type::MINIMAL_EXTRACTION,
        "minimal_extraction",
        "Extract only essential components"
    );
    fallback_options_.push_back(minimal_fallback);

    // Fallback 4: Skip extraction (for optional dependencies)
    auto skip_fallback = std::make_shared<FallbackOption>(
        FallbackOption::Type::SKIP_EXTRACTION,
        "skip_extraction",
        "Skip extraction and continue without dependency"
    );
    skip_fallback->set_condition([this]() {
        return is_optional_dependency();
    });
    fallback_options_.push_back(skip_fallback);
}

bool ExtractionErrorHandler::extract_with_fallback(
    const std::string& library_name,
    const std::string& source_path,
    const std::string& target_path,
    std::function<bool(const std::string&, const std::string&, const std::string&)> extraction_func) {

    log_info("Starting extraction with fallback support for: " + library_name);

    // Reset retry strategy
    retry_strategy_->reset();

    // Primary extraction with retry logic
    while (retry_strategy_->should_retry(ExtractionError())) {
        try {
            log_info("Attempt " + std::to_string(retry_strategy_->get_current_attempt() + 1) +
                     " of " + std::to_string(retry_strategy_->get_max_attempts()));

            if (extraction_func(library_name, source_path, target_path)) {
                log_success("Extraction successful for: " + library_name);
                cache_extraction(library_name, source_path, target_path);
                return true;
            }

        } catch (const std::exception& e) {
            ExtractionError error(ExtractionError::Type::UNKNOWN_ERROR, e.what(), "Primary extraction");
            record_error(error);

            if (retry_strategy_->should_retry(error)) {
                auto delay = retry_strategy_->get_delay();
                log_warning("Extraction failed, retrying in " +
                          std::to_string(delay.count()) + "ms...");
                std::this_thread::sleep_for(delay);
                retry_strategy_->increment_attempt();
            }
        }
    }

    log_warning("Primary extraction failed, attempting fallback options...");

    // Try fallback options
    for (auto& fallback : fallback_options_) {
        if (!fallback->is_available()) {
            continue;
        }

        log_info("Trying fallback option: " + fallback->get_name() +
                 " - " + fallback->get_description());

        if (fallback->execute([&, fallback, library_name, source_path, target_path]() {
            return execute_fallback(fallback->get_type(), library_name, source_path, target_path);
        })) {
            log_success("Fallback option succeeded: " + fallback->get_name());
            return true;
        }
    }

    // All attempts failed
    log_error("All extraction attempts failed for: " + library_name);
    return false;
}

bool ExtractionErrorHandler::execute_fallback(
    FallbackOption::Type type,
    const std::string& library_name,
    const std::string& source_path,
    const std::string& target_path) {

    switch (type) {
        case FallbackOption::Type::USE_CACHED:
            return restore_cached_extraction(library_name, target_path);

        case FallbackOption::Type::USE_SYSTEM_PACKAGE:
            return install_system_package(library_name);

        case FallbackOption::Type::MINIMAL_EXTRACTION:
            return perform_minimal_extraction(library_name, source_path, target_path);

        case FallbackOption::Type::SKIP_EXTRACTION:
            return handle_optional_dependency(library_name);

        default:
            return false;
    }
}

bool ExtractionErrorHandler::has_cached_extraction() const {
    // Check if we have a cached extraction in .cache directory
    std::filesystem::path cache_dir = project_root_ / ".cache/extracted";
    return std::filesystem::exists(cache_dir);
}

bool ExtractionErrorHandler::can_use_system_package() const {
    // Check if the library is available through system package manager
    return std::filesystem::exists("/usr/bin/apt-get") ||
           std::filesystem::exists("/usr/bin/yum") ||
           std::filesystem::exists("/usr/bin/pacman");
}

bool ExtractionErrorHandler::is_optional_dependency() const {
    // Check if the library is marked as optional in configuration
    return optional_libraries_.find(current_library_) != optional_libraries_.end();
}

bool ExtractionErrorHandler::restore_cached_extraction(
    const std::string& library_name,
    const std::string& target_path) {

    std::filesystem::path cache_dir = project_root_ / ".cache/extracted" / library_name;
    if (!std::filesystem::exists(cache_dir)) {
        return false;
    }

    try {
        // Copy cached extraction to target path
        std::filesystem::copy(cache_dir, target_path,
                                std::filesystem::copy_options::recursive);
        log_info("Restored cached extraction for: " + library_name);
        return true;
    } catch (const std::exception& e) {
        log_error("Failed to restore cached extraction: " + std::string(e.what()));
        return false;
    }
}

bool ExtractionErrorHandler::install_system_package(const std::string& library_name) {
    log_info("Installing " + library_name + " from system package manager...");

    // Map library names to package names
    std::map<std::string, std::string> package_map = {
        {"secp256k1-zkp", "libsecp256k1-dev"},
        {"nlohmann-json", "nlohmann-json-dev"},
        {"openssl", "libssl-dev"}
    };

    auto it = package_map.find(library_name);
    if (it == package_map.end()) {
        log_warning("No package mapping for: " + library_name);
        return false;
    }

    std::string package_name = it->second;

    // Try different package managers
    if (std::filesystem::exists("/usr/bin/apt-get")) {
        return execute_command("apt-get install -y " + package_name);
    } else if (std::filesystem::exists("/usr/bin/yum")) {
        return execute_command("yum install -y " + package_name);
    } else if (std::filesystem::exists("/usr/bin/pacman")) {
        return execute_command("pacman -S " + package_name);
    }

    return false;
}

bool ExtractionErrorHandler::perform_minimal_extraction(
    const std::string& library_name,
    const std::string& source_path,
    const std::string& target_path) {

    log_info("Performing minimal extraction for: " + library_name);

    // For minimal extraction, only copy essential files
    std::vector<std::string> essential_patterns = {
        "*.h",
        "include/**",
        "src/**"
    };

    try {
        // Create target directory
        std::filesystem::create_directories(target_path);

        for (const auto& pattern : essential_patterns) {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(source_path)) {
                if (entry.path().filename().string().find(".") == std::string::npos) {
                    continue;  // Skip files without extension
                }

                std::string file_path = entry.path().string();
                if (file_path.find(pattern) != std::string::npos) {
                    std::filesystem::path relative_path = file_path.substr(source_path.length());
                    std::filesystem::path target_file = target_path / relative_path;

                    std::filesystem::create_directories(target_file.parent_path());
                    std::filesystem::copy_file(file_path, target_file);
                }
            }
        }

        log_success("Minimal extraction completed for: " + library_name);
        return true;
    } catch (const std::exception& e) {
        log_error("Minimal extraction failed: " + std::string(e.what()));
        return false;
    }
}

bool ExtractionErrorHandler::handle_optional_dependency(const std::string& library_name) {
    log_info("Skipping optional dependency: " + library_name);
    log_warning("Build may continue with limited functionality");
    return true;
}

void ExtractionErrorHandler::cache_extraction(
    const std::string& library_name,
    const std::string& source_path,
    const::string& target_path) {

    try {
        std::filesystem::path cache_dir = project_root_ / ".cache/extracted" / library_name;
        std::filesystem::create_directories(cache_dir);

        // Copy extracted files to cache
        if (std::filesystem::exists(target_path)) {
            std::filesystem::copy(target_path, cache_dir,
                                    std::filesystem::copy_options::recursive);
            log_info("Cached extraction for: " + library_name);
        }
    } catch (const std::exception& e) {
        log_warning("Failed to cache extraction: " + std::string(e.what()));
    }
}

void ExtractionErrorHandler::record_error(const ExtractionError& error) {
    errors_.push_back(error);

    if (logging_enabled_) {
        log_error("Extraction error [" + error.type_to_string() + "]: " +
                  error.get_message() + " (Context: " + error.get_context() + ")");
    }
}

void ExtractionErrorHandler::set_project_root(const std::string& root) {
    project_root_ = root;
    std::filesystem::create_directories(project_root_ / ".cache");
}

void ExtractionErrorHandler::set_optional_libraries(const std::set<std::string>& libraries) {
    optional_libraries_ = libraries;
}

void ExtractionErrorHandler::set_current_library(const std::string& library_name) {
    current_library_ = library_name;
}

void ExtractionErrorHandler::enable_logging(bool enabled) {
    logging_enabled_ = enabled;
}

std::vector<ExtractionError> ExtractionErrorHandler::get_errors() const {
    return errors_;
}

void ExtractionErrorHandler::clear_errors() {
    errors_.clear();
}

bool ExtractionErrorHandler::execute_command(const std::string& command) {
    log_info("Executing: " + command);

    int result = std::system(command.c_str());
    return result == 0;
}

void ExtractionErrorHandler::log_info(const std::string& message) {
    if (logging_enabled_) {
        std::cout << "[INFO] ExtractionErrorHandler: " << message << std::endl;
    }
}

void ExtractionErrorHandler::log_warning(const std::string& message) {
    if (logging_enabled_) {
        std::cout << "[WARNING] ExtractionErrorHandler: " << message << std::endl;
    }
}

void ExtractionErrorHandler::log_error(const std::string& message) {
    if (logging_enabled_) {
        std::cerr << "[ERROR] ExtractionErrorHandler: " << message << std::endl;
    }
}

void ExtractionErrorHandler::log_success(const std::string& message) {
    if (logging_enabled_) {
        std::cout << "[SUCCESS] ExtractionErrorHandler: " << message << std::endl;
    }
}

// Utility functions
std::unique_ptr<ExtractionErrorHandler> create_error_handler() {
    return std::make_unique<ExtractionErrorHandler>();
}

bool extract_with_error_handling(
    const std::string& library_name,
    const std::string& source_path,
    const std::string& target_path,
    std::function<bool(const std::string&, const std::string&, const std::string&)> extraction_func,
    ExtractionErrorHandler* handler) {

    if (!handler) {
        handler = create_error_handler().release();
    }

    handler->set_current_library(library_name);
    return handler->extract_with_fallback(library_name, source_path, target_path, extraction_func);
}

} // namespace extraction
} // namespace integration