#include "services/operator_metadata_validator.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iomanip>
#include <regex>
#include <ctime>
#include <random>

namespace puzzle71::services {

OperatorMetadataValidator::OperatorMetadataValidator(ValidationLevel default_level)
    : default_validation_level_(default_level)
    , strict_mode_enabled_(true) {

    // Initialize default role requirements
    purpose_role_requirements_ = {
        {OperatorPurpose::SYSTEM_MAINTENANCE, {OperatorRole::ADMINISTRATOR, OperatorRole::OPERATOR}},
        {OperatorPurpose::PERFORMANCE_ANALYSIS, {OperatorRole::ANALYST, OperatorRole::ADMINISTRATOR, OperatorRole::DEVELOPER}},
        {OperatorPurpose::DEPLOYMENT, {OperatorRole::DEVELOPER, OperatorRole::ADMINISTRATOR}},
        {OperatorPurpose::DATA_PROCESSING, {OperatorRole::OPERATOR, OperatorRole::DEVELOPER, OperatorRole::ANALYST}},
        {OperatorPurpose::DEBUGGING, {OperatorRole::DEVELOPER, OperatorRole::ADMINISTRATOR}},
        {OperatorPurpose::TESTING, {OperatorRole::DEVELOPER, OperatorRole::ANALYST}},
        {OperatorPurpose::MONITORING, {OperatorRole::OPERATOR, OperatorRole::ANALYST, OperatorRole::ADMINISTRATOR}},
        {OperatorPurpose::BACKUP_RESTORE, {OperatorRole::ADMINISTRATOR, OperatorRole::OPERATOR}},
        {OperatorPurpose::SECURITY_AUDIT, {OperatorRole::AUDITOR, OperatorRole::ADMINISTRATOR}},
        {OperatorPurpose::RESEARCH, {OperatorRole::DEVELOPER, OperatorRole::ANALYST}},
        {OperatorPurpose::PRODUCTION, {OperatorRole::OPERATOR, OperatorRole::ADMINISTRATOR}}
    };

    // Initialize approval requirements
    approval_required_purposes_ = {
        OperatorPurpose::SYSTEM_MAINTENANCE,
        OperatorPurpose::DEPLOYMENT,
        OperatorPurpose::BACKUP_RESTORE,
        OperatorPurpose::SECURITY_AUDIT
    };

    // Initialize default environment restrictions
    environment_restrictions_["production"] = {OperatorRole::ADMINISTRATOR, OperatorRole::OPERATOR};
    environment_restrictions_["staging"] = {OperatorRole::DEVELOPER, OperatorRole::ADMINISTRATOR, OperatorRole::OPERATOR};
    environment_restrictions_["development"] = {OperatorRole::DEVELOPER, OperatorRole::ANALYST, OperatorRole::ADMINISTRATOR};

    // Initialize validation rules
    InitializeValidationRules();
}

void OperatorMetadataValidator::InitializeValidationRules() {
    // Operator ID format validation
    operator_id_validators_.push_back([this](const std::string& id) {
        return IsValidOperatorIdFormat(id);
    });

    // Operation metadata validation
    operation_validators_.push_back([this](const OperationMetadata& metadata) {
        return IsOperatorActive(metadata.operator_id);
    });

    operation_validators_.push_back([this](const OperationMetadata& metadata) {
        return HasRequiredRole(metadata.operator_id, metadata.purpose);
    });

    operation_validators_.push_back([this](const OperationMetadata& metadata) {
        return CanAccessEnvironment(metadata.operator_id, metadata.environment);
    });
}

bool OperatorMetadataValidator::RegisterOperator(const OperatorIdentifier& operator_info) {
    std::lock_guard<std::mutex> lock(validator_mutex_);

    // Validate operator info
    if (!IsValidOperatorIdFormat(operator_info.operator_id)) {
        return false;
    }

    // Check if operator already exists
    if (operators_.find(operator_info.operator_id) != operators_.end()) {
        return false;
    }

    // Set creation time if not provided
    OperatorIdentifier info = operator_info;
    if (info.created_at == std::chrono::system_clock::time_point{}) {
        info.created_at = std::chrono::system_clock::now();
    }

    info.last_active = info.created_at;
    operators_[operator_info.operator_id] = info;

    return true;
}

bool OperatorMetadataValidator::UpdateOperator(const std::string& operator_id, const OperatorIdentifier& updated_info) {
    std::lock_guard<std::mutex> lock(validator_mutex_);

    auto it = operators_.find(operator_id);
    if (it == operators_.end()) {
        return false;
    }

    // Preserve creation time and update last active
    OperatorIdentifier updated = updated_info;
    updated.created_at = it->second.created_at;
    updated.last_active = std::chrono::system_clock::now();

    operators_[operator_id] = updated;
    return true;
}

bool OperatorMetadataValidator::DeactivateOperator(const std::string& operator_id) {
    std::lock_guard<std::mutex> lock(validator_mutex_);

    auto it = operators_.find(operator_id);
    if (it == operators_.end()) {
        return false;
    }

    it->second.is_active = false;
    it->second.last_active = std::chrono::system_clock::now();
    return true;
}

bool OperatorMetadataValidator::IsOperatorAuthorized(const std::string& operator_id) const {
    std::lock_guard<std::mutex> lock(validator_mutex_);

    auto it = operators_.find(operator_id);
    if (it == operators_.end()) {
        return false;
    }

    return it->second.is_active && it->second.is_authorized;
}

OperatorIdentifier OperatorMetadataValidator::GetOperatorInfo(const std::string& operator_id) const {
    std::lock_guard<std::mutex> lock(validator_mutex_);

    auto it = operators_.find(operator_id);
    if (it != operators_.end()) {
        return it->second;
    }

    return OperatorIdentifier{};
}

ValidationResult OperatorMetadataValidator::ValidateOperationMetadata(const OperationMetadata& metadata) {
    auto start_time = std::chrono::high_resolution_clock::now();
    ValidationResult result;
    result.achieved_level = default_validation_level_;
    result.confidence_score = 1.0;

    try {
        // Basic validation
        auto completeness_errors = ValidateOperatorMetadataCompleteness(metadata);
        result.validation_errors.insert(result.validation_errors.end(),
                                       completeness_errors.begin(), completeness_errors.end());

        // Format validation
        if (!IsValidOperatorIdFormat(metadata.operator_id)) {
            result.validation_errors.push_back("Invalid operator ID format");
        }

        // Operator validation
        if (!IsOperatorActive(metadata.operator_id)) {
            result.validation_errors.push_back("Operator is not active or does not exist");
            result.confidence_score *= 0.5;
        }

        // Role validation
        if (!HasRequiredRole(metadata.operator_id, metadata.purpose)) {
            result.validation_errors.push_back("Operator does not have required role for this purpose");
            result.confidence_score *= 0.3;
        }

        // Environment validation
        if (!CanAccessEnvironment(metadata.operator_id, metadata.environment)) {
            result.validation_errors.push_back("Operator is not authorized for this environment");
            result.confidence_score *= 0.2;
        }

        // Timing validation
        auto timing_warnings = ValidateOperationTiming(metadata);
        result.validation_warnings.insert(result.validation_warnings.end(),
                                          timing_warnings.begin(), timing_warnings.end());

        // Security validation
        auto security_errors = ValidateSecurityConstraints(metadata);
        result.validation_errors.insert(result.validation_errors.end(),
                                       security_errors.begin(), security_errors.end());

        // Approval validation
        if (RequiresApproval(metadata) && !metadata.is_approved) {
            result.validation_warnings.push_back("Operation requires approval but is not approved");
            result.confidence_score *= 0.8;
        }

        // Set validation result
        result.is_valid = result.validation_errors.empty();
        if (!result.validation_errors.empty()) {
            result.achieved_level = ValidationLevel::BASIC;
        } else if (!result.validation_warnings.empty()) {
            result.achieved_level = ValidationLevel::STRICT;
        } else {
            result.achieved_level = ValidationLevel::COMPREHENSIVE;
        }

    } catch (const std::exception& e) {
        result.is_valid = false;
        result.validation_errors.push_back(std::string("Validation exception: ") + e.what());
        result.confidence_score = 0.0;
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    result.validation_time = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

    return result;
}

ValidationResult OperatorMetadataValidator::ValidateOperatorId(const std::string& operator_id) {
    ValidationResult result;
    result.achieved_level = ValidationLevel::BASIC;
    result.confidence_score = 1.0;

    auto start_time = std::chrono::high_resolution_clock::now();

    // Format validation
    if (!IsValidOperatorIdFormat(operator_id)) {
        result.validation_errors.push_back("Invalid operator ID format");
        result.is_valid = false;
        result.confidence_score = 0.0;
    } else {
        // Existence validation
        auto it = operators_.find(operator_id);
        if (it == operators_.end()) {
            result.validation_warnings.push_back("Operator ID is valid but not registered");
            result.confidence_score = 0.7;
        } else {
            // Active status validation
            if (!it->second.is_active) {
                result.validation_errors.push_back("Operator is not active");
                result.is_valid = false;
                result.confidence_score = 0.0;
            } else {
                result.is_valid = true;
                result.achieved_level = ValidationLevel::COMPREHENSIVE;
            }
        }
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    result.validation_time = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

    return result;
}

ValidationResult OperatorMetadataValidator::ValidateOperatorPurpose(const OperatorPurpose purpose, const std::string& operator_id) {
    ValidationResult result;
    result.achieved_level = ValidationLevel::STRICT;
    result.confidence_score = 1.0;

    auto start_time = std::chrono::high_resolution_clock::now();

    // Validate operator exists and is active
    auto operator_it = operators_.find(operator_id);
    if (operator_it == operators_.end()) {
        result.validation_errors.push_back("Operator does not exist");
        result.is_valid = false;
        result.confidence_score = 0.0;
        return result;
    }

    if (!operator_it->second.is_active) {
        result.validation_errors.push_back("Operator is not active");
        result.is_valid = false;
        result.confidence_score = 0.0;
        return result;
    }

    // Validate role requirements
    auto purpose_it = purpose_role_requirements_.find(purpose);
    if (purpose_it != purpose_role_requirements_.end()) {
        const auto& required_roles = purpose_it->second;
        if (std::find(required_roles.begin(), required_roles.end(), operator_it->second.role) == required_roles.end()) {
            result.validation_errors.push_back("Operator does not have required role for this purpose");
            result.is_valid = false;
            result.confidence_score = 0.0;
        } else {
            result.is_valid = true;
            result.achieved_level = ValidationLevel::COMPREHENSIVE;
        }
    } else {
        // No specific requirements for this purpose
        result.validation_warnings.push_back("No specific role requirements defined for this purpose");
        result.is_valid = true;
        result.confidence_score = 0.9;
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    result.validation_time = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

    return result;
}

ValidationResult OperatorMetadataValidator::ValidateSessionIntegrity(const std::string& session_id, const std::string& operator_id) {
    ValidationResult result;
    result.achieved_level = ValidationLevel::BASIC;
    result.confidence_score = 1.0;

    auto start_time = std::chrono::high_resolution_clock::now();

    // Basic session ID format validation
    static const std::regex session_pattern(R"^[a-f0-9]{8}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{12}$");
    if (!std::regex_match(session_id, session_pattern)) {
        result.validation_errors.push_back("Invalid session ID format");
        result.is_valid = false;
        result.confidence_score = 0.0;
    } else {
        result.is_valid = true;
        result.achieved_level = ValidationLevel::COMPREHENSIVE;
    }

    // Check if operator exists
    if (operators_.find(operator_id) == operators_.end()) {
        result.validation_warnings.push_back("Operator ID not found in system");
        result.confidence_score *= 0.8;
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    result.validation_time = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

    return result;
}

ValidationResult OperatorMetadataValidator::ValidateEnvironmentAccess(const std::string& environment, const std::string& operator_id) {
    ValidationResult result;
    result.achieved_level = ValidationLevel::STRICT;
    result.confidence_score = 1.0;

    auto start_time = std::chrono::high_resolution_clock::now();

    // Check if environment exists in restrictions
    auto env_it = environment_restrictions_.find(environment);
    if (env_it == environment_restrictions_.end()) {
        result.validation_warnings.push_back("Environment has no specific access restrictions");
        result.is_valid = true;
        result.confidence_score = 0.9;
    } else {
        // Check operator role
        auto operator_it = operators_.find(operator_id);
        if (operator_it == operators_.end()) {
            result.validation_errors.push_back("Operator does not exist");
            result.is_valid = false;
            result.confidence_score = 0.0;
        } else {
            const auto& allowed_roles = env_it->second;
            if (std::find(allowed_roles.begin(), allowed_roles.end(), operator_it->second.role) == allowed_roles.end()) {
                result.validation_errors.push_back("Operator role not authorized for this environment");
                result.is_valid = false;
                result.confidence_score = 0.0;
            } else {
                result.is_valid = true;
                result.achieved_level = ValidationLevel::COMPREHENSIVE;
            }
        }
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    result.validation_time = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

    return result;
}

bool OperatorMetadataValidator::RecordOperation(const OperationMetadata& metadata) {
    std::lock_guard<std::mutex> lock(validator_mutex_);

    // Validate operation metadata first
    auto validation = ValidateOperationMetadata(metadata);
    if (!validation.is_valid && strict_mode_enabled_) {
        return false;
    }

    // Record operation
    OperationMetadata record = metadata;
    if (record.operation_id.empty()) {
        record.operation_id = GenerateOperationId();
    }

    if (record.start_time == std::chrono::system_clock::time_point{}) {
        record.start_time = std::chrono::system_clock::now();
    }

    record.validation_level = default_validation_level_;

    // Add to active operations
    active_operations_[record.operation_id] = record;

    // Add to operator history
    operator_history_[record.operator_id].push_back(record);

    return true;
}

bool OperatorMetadataValidator::UpdateOperationResult(const std::string& operation_id, const json& result_summary) {
    std::lock_guard<std::mutex> lock(validator_mutex_);

    auto it = active_operations_.find(operation_id);
    if (it == active_operations_.end()) {
        return false;
    }

    it->second.result_summary = result_summary;
    it->second.end_time = std::chrono::system_clock::now();

    return true;
}

bool OperatorMetadataValidator::ApproveOperation(const std::string& operation_id, const std::string& approver_id) {
    std::lock_guard<std::mutex> lock(validator_mutex_);

    // Check if approver is authorized
    auto approver_it = operators_.find(approver_id);
    if (approver_it == operators_.end() || !approver_it->second.is_active ||
        approver_it->second.role != OperatorRole::ADMINISTRATOR) {
        return false;
    }

    auto it = active_operations_.find(operation_id);
    if (it == active_operations_.end()) {
        return false;
    }

    it->second.is_approved = true;
    it->second.approver_id = approver_id;
    it->second.approval_time = std::chrono::system_clock::now();

    return true;
}

bool OperatorMetadataValidator::RejectOperation(const std::string& operation_id, const std::string& reason) {
    std::lock_guard<std::mutex> lock(validator_mutex_);

    auto it = active_operations_.find(operation_id);
    if (it == active_operations_.end()) {
        return false;
    }

    // Add rejection reason to metadata
    it->second.result_summary["rejection_reason"] = reason;
    it->second.result_summary["rejected_at"] = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    it->second.end_time = std::chrono::system_clock::now();

    // Remove from active operations
    active_operations_.erase(it);

    return true;
}

ComplianceReport OperatorMetadataValidator::GenerateComplianceReport(const std::string& start_date, const std::string& end_date) const {
    std::lock_guard<std::mutex> lock(validator_mutex_);

    ComplianceReport report;
    report.report_id = GenerateOperationId();
    report.report_timestamp = std::to_string(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));

    // Collect all operations
    std::vector<OperationMetadata> all_operations;
    for (const auto& [operator_id, operations] : operator_history_) {
        all_operations.insert(all_operations.end(), operations.begin(), operations.end());
    }

    // Filter by date range if specified
    if (!start_date.empty() || !end_date.empty()) {
        std::chrono::system_clock::time_point start_time, end_time;
        if (!start_date.empty()) {
            std::tm tm = {};
            std::istringstream ss(start_date);
            ss >> std::get_time(&tm, "%Y-%m-%d");
            start_time = std::chrono::system_clock::from_time_t(std::mktime(&tm));
        }
        if (!end_date.empty()) {
            std::tm tm = {};
            std::istringstream ss(end_date);
            ss >> std::get_time(&tm, "%Y-%m-%d");
            end_time = std::chrono::system_clock::from_time_t(std::mktime(&tm));
        }

        auto new_end = std::remove_if(all_operations.begin(), all_operations.end(),
            [&start_time, &end_time](const OperationMetadata& op) {
                if (!start_date.empty() && op.start_time < start_time) return true;
                if (!end_date.empty() && op.start_time > end_time) return true;
                return false;
            });
        all_operations.erase(new_end, all_operations.end());
    }

    report.total_operations_checked = all_operations.size();
    report.operation_records = all_operations;

    // Validate each operation
    for (const auto& operation : all_operations) {
        auto result = ValidateOperationMetadata(operation);
        report.validation_results.push_back(result);

        if (result.is_valid) {
            report.valid_operations++;
        } else {
            report.invalid_operations++;
        }

        if (!result.validation_warnings.empty()) {
            report.operations_with_warnings++;
        }
    }

    // Calculate compliance metrics
    report.overall_compliance_score = CalculateComplianceScore(report);
    report.meets_compliance_requirements = report.overall_compliance_score >= 95.0;

    // Identify violations and concerns
    report.compliance_violations = IdentifyComplianceViolations(report);

    // Generate recommendations
    if (report.overall_compliance_score < 95.0) {
        report.recommendations.push_back("Review and address validation failures to improve compliance");
    }
    if (report.invalid_operations > 0) {
        report.recommendations.push_back("Investigate and resolve invalid operations");
    }

    return report;
}

std::vector<OperationMetadata> OperatorMetadataValidator::GetOperatorHistory(const std::string& operator_id, size_t limit) const {
    std::lock_guard<std::mutex> lock(validator_mutex_);

    auto it = operator_history_.find(operator_id);
    if (it == operator_history_.end()) {
        return {};
    }

    auto history = it->second;
    std::sort(history.begin(), history.end(),
        [](const OperationMetadata& a, const OperationMetadata& b) {
            return a.start_time > b.start_time;
        });

    if (limit > 0 && history.size() > limit) {
        history.resize(limit);
    }

    return history;
}

std::vector<std::string> OperatorMetadataValidator::GetUnauthorizedOperations() const {
    std::lock_guard<std::mutex> lock(validator_mutex_);

    std::vector<std::string> unauthorized_ops;
    for (const auto& [operator_id, operations] : operator_history_) {
        for (const auto& operation : operations) {
            auto result = ValidateOperationMetadata(operation);
            if (!result.is_valid) {
                unauthorized_ops.push_back(operation.operation_id);
            }
        }
    }

    return unauthorized_ops;
}

bool OperatorMetadataValidator::DetectAnomalousBehavior(const std::string& operator_id) const {
    std::lock_guard<std::mutex> lock(validator_mutex_);

    auto it = operator_history_.find(operator_id);
    if (it == operator_history_.end() || it->second.empty()) {
        return false;
    }

    const auto& history = it->second;

    // Check for unusual activity patterns
    // 1. High frequency of operations in short time
    auto now = std::chrono::system_clock::now();
    auto recent_threshold = now - std::chrono::hours(1);

    size_t recent_operations = std::count_if(history.begin(), history.end(),
        [recent_threshold](const OperationMetadata& op) {
            return op.start_time > recent_threshold;
        });

    if (recent_operations > 100) { // More than 100 operations in an hour
        return true;
    }

    // 2. Failed operations pattern
    size_t failed_operations = std::count_if(history.begin(), history.end(),
        [this](const OperationMetadata& op) {
            auto result = ValidateOperationMetadata(op);
            return !result.is_valid;
        });

    if (failed_operations > history.size() * 0.3) { // More than 30% failure rate
        return true;
    }

    return false;
}

// Configuration methods

void OperatorMetadataValidator::SetValidationLevel(ValidationLevel level) {
    std::lock_guard<std::mutex> lock(validator_mutex_);
    default_validation_level_ = level;
}

void OperatorMetadataValidator::SetOperatorRoleRequirements(const std::map<OperatorPurpose, std::vector<OperatorRole>>& requirements) {
    std::lock_guard<std::mutex> lock(validator_mutex_);
    purpose_role_requirements_ = requirements;
}

void OperatorMetadataValidator::AddRestrictedEnvironment(const std::string& environment, const std::vector<OperatorRole>& allowed_roles) {
    std::lock_guard<std::mutex> lock(validator_mutex_);
    environment_restrictions_[environment] = allowed_roles;
}

void OperatorMetadataValidator::EnableStrictMode(bool enabled) {
    std::lock_guard<std::mutex> lock(validator_mutex_);
    strict_mode_enabled_ = enabled;
}

void OperatorMetadataValidator::SetApprovalRequired(const std::vector<OperatorPurpose>& purposes) {
    std::lock_guard<std::mutex> lock(validator_mutex_);
    approval_required_purposes_.clear();
    approval_required_purposes_.insert(purposes.begin(), purposes.end());
}

// Security and access control methods

bool OperatorMetadataValidator::CheckPermission(const std::string& operator_id, const std::string& permission) const {
    std::lock_guard<std::mutex> lock(validator_mutex_);

    auto it = operators_.find(operator_id);
    if (it == operators_.end() || !it->second.is_active) {
        return false;
    }

    return std::find(it->second.permissions.begin(), it->second.permissions.end(), permission) !=
           it->second.permissions.end();
}

bool OperatorMetadataValidator::ValidateIPAddress(const std::string& ip_address, const std::string& operator_id) const {
    // Basic IP address validation
    static const std::regex ipv4_pattern(R"^(\d{1,3}\.){3}\d{1,3}$");
    static const std::regex ipv6_pattern(R"^([0-9a-fA-F]{1,4}:){7}[0-9a-fA-F]{1,4}$");

    if (!std::regex_match(ip_address, ipv4_pattern) && !std::regex_match(ip_address, ipv6_pattern)) {
        return false;
    }

    // In a real implementation, you might check against allowed IP ranges
    return true;
}

bool OperatorMetadataValidator::ValidateSessionDuration(const std::string& session_id, std::chrono::hours max_duration) const {
    // Find the session start time (this is a simplified implementation)
    // In practice, you would track session start times
    auto session_start = std::chrono::system_clock::now() - std::chrono::hours(1); // Placeholder
    auto session_duration = std::chrono::system_clock::now() - session_start;

    return session_duration <= max_duration;
}

std::vector<std::string> OperatorMetadataValidator::GetSuspiciousActivities(const std::string& operator_id) const {
    std::vector<std::string> activities;

    if (DetectAnomalousBehavior(operator_id)) {
        activities.push_back("Unusual activity pattern detected");
    }

    // Check for multiple failed login attempts
    auto history = GetOperatorHistory(operator_id, 50);
    size_t failed_attempts = std::count_if(history.begin(), history.end(),
        [](const OperationMetadata& op) {
            return op.purpose == OperatorPurpose::DEBUGGING && !op.is_approved;
        });

    if (failed_attempts > 5) {
        activities.push_back("Multiple failed operations detected");
    }

    return activities;
}

// Utility methods

std::string OperatorMetadataValidator::GenerateOperationId() const {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    static const char hex[] = "0123456789abcdef";

    std::string result = "op_";
    for (int i = 0; i < 16; ++i) {
        result += hex[dis(gen)];
    }

    return result;
}

std::string OperatorMetadataValidator::GenerateSessionId(const std::string& operator_id) const {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    static const char hex[] = "0123456789abcdef";

    std::string result = "sess_";
    for (int i = 0; i < 8; ++i) {
        if (i == 4 || i == 6) result += "-";
        result += hex[dis(gen)];
    }
    result += "-" + operator_id.substr(0, 8);

    return result;
}

OperatorPurpose OperatorMetadataValidator::StringToPurpose(const std::string& purpose_str) const {
    static const std::map<std::string, OperatorPurpose> purpose_map = {
        {"system_maintenance", OperatorPurpose::SYSTEM_MAINTENANCE},
        {"performance_analysis", OperatorPurpose::PERFORMANCE_ANALYSIS},
        {"deployment", OperatorPurpose::DEPLOYMENT},
        {"data_processing", OperatorPurpose::DATA_PROCESSING},
        {"debugging", OperatorPurpose::DEBUGGING},
        {"testing", OperatorPurpose::TESTING},
        {"monitoring", OperatorPurpose::MONITORING},
        {"backup_restore", OperatorPurpose::BACKUP_RESTORE},
        {"security_audit", OperatorPurpose::SECURITY_AUDIT},
        {"research", OperatorPurpose::RESEARCH},
        {"production", OperatorPurpose::PRODUCTION}
    };

    auto it = purpose_map.find(purpose_str);
    return it != purpose_map.end() ? it->second : OperatorPurpose::UNKNOWN;
}

std::string OperatorMetadataValidator::PurposeToString(const OperatorPurpose purpose) const {
    static const std::map<OperatorPurpose, std::string> purpose_map = {
        {OperatorPurpose::SYSTEM_MAINTENANCE, "system_maintenance"},
        {OperatorPurpose::PERFORMANCE_ANALYSIS, "performance_analysis"},
        {OperatorPurpose::DEPLOYMENT, "deployment"},
        {OperatorPurpose::DATA_PROCESSING, "data_processing"},
        {OperatorPurpose::DEBUGGING, "debugging"},
        {OperatorPurpose::TESTING, "testing"},
        {OperatorPurpose::MONITORING, "monitoring"},
        {OperatorPurpose::BACKUP_RESTORE, "backup_restore"},
        {OperatorPurpose::SECURITY_AUDIT, "security_audit"},
        {OperatorPurpose::RESEARCH, "research"},
        {OperatorPurpose::PRODUCTION, "production"},
        {OperatorPurpose::UNKNOWN, "unknown"}
    };

    auto it = purpose_map.find(purpose);
    return it != purpose_map.end() ? it->second : "unknown";
}

OperatorRole OperatorMetadataValidator::StringToRole(const std::string& role_str) const {
    static const std::map<std::string, OperatorRole> role_map = {
        {"administrator", OperatorRole::ADMINISTRATOR},
        {"developer", OperatorRole::DEVELOPER},
        {"analyst", OperatorRole::ANALYST},
        {"operator", OperatorRole::OPERATOR},
        {"auditor", OperatorRole::AUDITOR},
        {"automated_system", OperatorRole::AUTOMATED_SYSTEM}
    };

    auto it = role_map.find(role_str);
    return it != role_map.end() ? it->second : OperatorRole::UNKNOWN;
}

std::string OperatorMetadataValidator::RoleToString(const OperatorRole role) const {
    static const std::map<OperatorRole, std::string> role_map = {
        {OperatorRole::ADMINISTRATOR, "administrator"},
        {OperatorRole::DEVELOPER, "developer"},
        {OperatorRole::ANALYST, "analyst"},
        {OperatorRole::OPERATOR, "operator"},
        {OperatorRole::AUDITOR, "auditor"},
        {OperatorRole::AUTOMATED_SYSTEM, "automated_system"},
        {OperatorRole::UNKNOWN, "unknown"}
    };

    auto it = role_map.find(role);
    return it != role_map.end() ? it->second : "unknown";
}

// Private methods

bool OperatorMetadataValidator::IsValidOperatorIdFormat(const std::string& operator_id) const {
    // Format: 2-3 letters + 3-5 digits (e.g., "OP123" or "ADM45678")
    static const std::regex operator_id_pattern(R"^[A-Za-z]{2,3}\d{3,5}$");
    return std::regex_match(operator_id, operator_id_pattern);
}

bool OperatorMetadataValidator::IsOperatorActive(const std::string& operator_id) const {
    auto it = operators_.find(operator_id);
    return it != operators_.end() && it->second.is_active;
}

bool OperatorMetadataValidator::HasRequiredRole(const std::string& operator_id, OperatorPurpose purpose) const {
    auto operator_it = operators_.find(operator_id);
    if (operator_it == operators_.end()) {
        return false;
    }

    auto purpose_it = purpose_role_requirements_.find(purpose);
    if (purpose_it == purpose_role_requirements_.end()) {
        return true; // No specific requirements
    }

    const auto& required_roles = purpose_it->second;
    return std::find(required_roles.begin(), required_roles.end(), operator_it->second.role) != required_roles.end();
}

bool OperatorMetadataValidator::CanAccessEnvironment(const std::string& operator_id, const std::string& environment) const {
    auto operator_it = operators_.find(operator_id);
    if (operator_it == operators_.end()) {
        return false;
    }

    auto env_it = environment_restrictions_.find(environment);
    if (env_it == environment_restrictions_.end()) {
        return true; // No restrictions for this environment
    }

    const auto& allowed_roles = env_it->second;
    return std::find(allowed_roles.begin(), allowed_roles.end(), operator_it->second.role) != allowed_roles.end();
}

bool OperatorMetadataValidator::RequiresApproval(const OperationMetadata& metadata) const {
    return approval_required_purposes_.count(metadata.purpose) > 0;
}

std::vector<std::string> OperatorMetadataValidator::ValidateOperatorMetadataCompleteness(const OperationMetadata& metadata) const {
    std::vector<std::string> errors;

    if (metadata.operator_id.empty()) {
        errors.push_back("Operator ID is required");
    }

    if (metadata.operation_description.empty()) {
        errors.push_back("Operation description is required");
    }

    if (metadata.environment.empty()) {
        errors.push_back("Environment is required");
    }

    if (metadata.purpose == OperatorPurpose::UNKNOWN) {
        errors.push_back("Valid operation purpose is required");
    }

    return errors;
}

std::vector<std::string> OperatorMetadataValidator::ValidateOperationTiming(const OperationMetadata& metadata) const {
    std::vector<std::string> warnings;

    auto now = std::chrono::system_clock::now();
    auto operation_age = now - metadata.start_time;

    if (operation_age > std::chrono::hours(24)) {
        warnings.push_back("Operation metadata is more than 24 hours old");
    }

    if (metadata.end_time != std::chrono::system_clock::time_point{} &&
        metadata.end_time < metadata.start_time) {
        warnings.push_back("Operation end time is before start time");
    }

    return warnings;
}

std::vector<std::string> OperatorMetadataValidator::ValidateSecurityConstraints(const OperationMetadata& metadata) const {
    std::vector<std::string> errors;

    // Validate IP address format
    if (!metadata.source_ip_address.empty() && !ValidateIPAddress(metadata.source_ip_address, metadata.operator_id)) {
        errors.push_back("Invalid source IP address format");
    }

    // Validate session ID format
    if (!metadata.session_id.empty()) {
        static const std::regex session_pattern(R"^[a-f0-9]{8}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{12}$");
        if (!std::regex_match(metadata.session_id, session_pattern)) {
            errors.push_back("Invalid session ID format");
        }
    }

    return errors;
}

double OperatorMetadataValidator::CalculateComplianceScore(const ComplianceReport& report) const {
    if (report.total_operations_checked == 0) return 100.0;

    double valid_ratio = static_cast<double>(report.valid_operations) / report.total_operations_checked;
    double warning_penalty = static_cast<double>(report.operations_with_warnings) / report.total_operations_checked * 0.1;

    return std::max(0.0, (valid_ratio * 100.0) - (warning_penalty * 100.0));
}

std::vector<std::string> OperatorMetadataValidator::IdentifyComplianceViolations(const ComplianceReport& report) const {
    std::vector<std::string> violations;

    for (const auto& result : report.validation_results) {
        if (!result.is_valid) {
            for (const auto& error : result.validation_errors) {
                violations.push_back(error);
            }
        }
    }

    return violations;
}

// ScopedOperationValidator implementation

ScopedOperationValidator::ScopedOperationValidator(OperatorMetadataValidator& validator,
                                                   const OperationMetadata& metadata)
    : validator_(validator), metadata_(metadata), operation_completed_(false) {
    validation_result_ = validator_.ValidateOperationMetadata(metadata);
}

ScopedOperationValidator::~ScopedOperationValidator() {
    if (validation_result_.is_valid) {
        validator_.RecordOperation(metadata_);
    }
}

bool ScopedOperationValidator::IsValid() const {
    return validation_result_.is_valid;
}

const ValidationResult& ScopedOperationValidator::GetValidationResult() const {
    return validation_result_;
}

void ScopedOperationValidator::SetOperationResult(const json& result_summary) {
    if (!metadata_.operation_id.empty()) {
        validator_.UpdateOperationResult(metadata_.operation_id, result_summary);
    }
    operation_completed_ = true;
}

// OperatorMetadataTemplates implementation

OperationMetadata OperatorMetadataTemplates::CreateBaseTemplate(const std::string& operator_id, OperatorPurpose purpose) {
    OperationMetadata metadata;
    metadata.operator_id = operator_id;
    metadata.purpose = purpose;
    metadata.start_time = std::chrono::system_clock::now();
    metadata.validation_level = ValidationLevel::STRICT;
    metadata.is_approved = false;
    return metadata;
}

OperationMetadata OperatorMetadataTemplates::CreateSystemMaintenanceTemplate(const std::string& operator_id) {
    auto metadata = CreateBaseTemplate(operator_id, OperatorPurpose::SYSTEM_MAINTENANCE);
    metadata.operation_description = "System maintenance and updates";
    metadata.requires_approval = true;
    return metadata;
}

OperationMetadata OperatorMetadataTemplates::CreatePerformanceAnalysisTemplate(const std::string& operator_id) {
    auto metadata = CreateBaseTemplate(operator_id, OperatorPurpose::PERFORMANCE_ANALYSIS);
    metadata.operation_description = "Performance monitoring and analysis";
    metadata.tags = {"performance", "monitoring"};
    return metadata;
}

OperationMetadata OperatorMetadataTemplates::CreateDeploymentTemplate(const std::string& operator_id) {
    auto metadata = CreateBaseTemplate(operator_id, OperatorPurpose::DEPLOYMENT);
    metadata.operation_description = "Software deployment and configuration";
    metadata.requires_approval = true;
    metadata.tags = {"deployment", "configuration"};
    return metadata;
}

OperationMetadata OperatorMetadataTemplates::CreateDataProcessingTemplate(const std::string& operator_id) {
    auto metadata = CreateBaseTemplate(operator_id, OperatorPurpose::DATA_PROCESSING);
    metadata.operation_description = "Data processing operations";
    metadata.tags = {"data", "processing"};
    return metadata;
}

OperationMetadata OperatorMetadataTemplates::CreateDebuggingTemplate(const std::string& operator_id) {
    auto metadata = CreateBaseTemplate(operator_id, OperatorPurpose::DEBUGGING);
    metadata.operation_description = "Debugging and troubleshooting";
    metadata.tags = {"debug", "troubleshoot"};
    return metadata;
}

OperationMetadata OperatorMetadataTemplates::CreateTestingTemplate(const std::string& operator_id) {
    auto metadata = CreateBaseTemplate(operator_id, OperatorPurpose::TESTING);
    metadata.operation_description = "Testing and validation";
    metadata.tags = {"test", "validation"};
    return metadata;
}

OperationMetadata OperatorMetadataTemplates::CreateMonitoringTemplate(const std::string& operator_id) {
    auto metadata = CreateBaseTemplate(operator_id, OperatorPurpose::MONITORING);
    metadata.operation_description = "System monitoring and alerting";
    metadata.tags = {"monitor", "alert"};
    return metadata;
}

OperationMetadata OperatorMetadataTemplates::CreateSecurityAuditTemplate(const std::string& operator_id) {
    auto metadata = CreateBaseTemplate(operator_id, OperatorPurpose::SECURITY_AUDIT);
    metadata.operation_description = "Security auditing and compliance";
    metadata.requires_approval = true;
    metadata.tags = {"security", "audit", "compliance"};
    return metadata;
}

// Factory function

std::unique_ptr<OperatorMetadataValidator> CreateOperatorMetadataValidator(ValidationLevel default_level, bool strict_mode) {
    auto validator = std::make_unique<OperatorMetadataValidator>(default_level);
    validator->EnableStrictMode(strict_mode);
    return validator;
}

} // namespace puzzle71::services