#pragma once

#include <string>
#include <vector>
#include <memory>
#include <map>
#include <chrono>
#include <json/json.h>

namespace puzzle71::services {

/**
 * @brief Operator metadata validation and enforcement system
 *
 * Provides comprehensive validation for operator-id and operator-purpose metadata:
 * - Operator ID format validation and authentication checks
 * - Operator purpose classification and enforcement logic
 * - WORM audit logging integration for all operations
 * - Metadata completeness validation
 * - Access control and authorization verification
 * - Historical tracking and compliance reporting
 */

enum class OperatorRole {
    ADMINISTRATOR,       // System administrator with full privileges
    DEVELOPER,          // Software developer with deployment rights
    ANALYST,            // Data analyst with read/analysis rights
    OPERATOR,           // System operator with execution rights
    AUDITOR,            // Auditor with read-only access
    AUTOMATED_SYSTEM,   // Automated system/service
    UNKNOWN             // Unidentified or unauthorized role
};

enum class OperatorPurpose {
    SYSTEM_MAINTENANCE,  // System maintenance and updates
    PERFORMANCE_ANALYSIS, // Performance monitoring and analysis
    DEPLOYMENT,         // Software deployment and configuration
    DATA_PROCESSING,    // Data processing and computation
    DEBUGGING,          // Debugging and troubleshooting
    TESTING,            // Testing and validation
    MONITORING,         // System monitoring and alerting
    BACKUP_RESTORE,     // Backup and restore operations
    SECURITY_AUDIT,     // Security auditing and compliance
    RESEARCH,           // Research and development
    PRODUCTION,         // Production workloads
    UNKNOWN             // Unknown or unspecified purpose
};

enum class ValidationLevel {
    BASIC,              // Basic format validation only
    STRICT,             // Strict validation with authentication
    COMPREHENSIVE,      // Comprehensive validation with full checks
    AUDIT               // Audit-level validation with full logging
};

struct OperatorIdentifier {
    std::string operator_id;
    std::string operator_name;
    OperatorRole role;
    std::string department;
    std::string email;
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point last_active;
    bool is_active;
    bool is_authorized;
    std::vector<std::string> permissions;
    json metadata;
};

struct OperationMetadata {
    std::string operation_id;
    std::string operator_id;
    OperatorPurpose purpose;
    std::string operation_description;
    std::chrono::system_clock::time_point start_time;
    std::chrono::system_clock::time_point end_time;
    std::string session_id;
    std::string source_ip_address;
    std::string user_agent;
    std::string environment;
    ValidationLevel validation_level;
    bool requires_approval;
    bool is_approved;
    std::string approver_id;
    std::chrono::system_clock::time_point approval_time;
    json operation_parameters;
    json result_summary;
    std::vector<std::string> tags;
};

struct ValidationResult {
    bool is_valid;
    std::vector<std::string> validation_errors;
    std::vector<std::string> validation_warnings;
    ValidationLevel achieved_level;
    std::chrono::microseconds validation_time;
    double confidence_score; // 0.0-1.0
    json additional_info;
};

struct ComplianceReport {
    std::string report_id;
    std::string report_timestamp;
    size_t total_operations_checked;
    size_t valid_operations;
    size_t invalid_operations;
    size_t operations_with_warnings;

    std::vector<OperationMetadata> operation_records;
    std::vector<ValidationResult> validation_results;
    std::vector<std::string> compliance_violations;
    std::vector<std::string> security_concerns;

    double overall_compliance_score;
    bool meets_compliance_requirements;
    std::map<std::string, double> category_scores;
    std::vector<std::string> recommendations;
};

class OperatorMetadataValidator {
public:
    explicit OperatorMetadataValidator(ValidationLevel default_level = ValidationLevel::STRICT);
    ~OperatorMetadataValidator() = default;

    // Operator management
    bool RegisterOperator(const OperatorIdentifier& operator_info);
    bool UpdateOperator(const std::string& operator_id, const OperatorIdentifier& updated_info);
    bool DeactivateOperator(const std::string& operator_id);
    bool IsOperatorAuthorized(const std::string& operator_id) const;
    OperatorIdentifier GetOperatorInfo(const std::string& operator_id) const;

    // Operation validation
    ValidationResult ValidateOperationMetadata(const OperationMetadata& metadata);
    ValidationResult ValidateOperatorId(const std::string& operator_id);
    ValidationResult ValidateOperatorPurpose(const OperatorPurpose purpose, const std::string& operator_id);
    ValidationResult ValidateSessionIntegrity(const std::string& session_id, const std::string& operator_id);
    ValidationResult ValidateEnvironmentAccess(const std::string& environment, const std::string& operator_id);

    // Operation tracking
    bool RecordOperation(const OperationMetadata& metadata);
    bool UpdateOperationResult(const std::string& operation_id, const json& result_summary);
    bool ApproveOperation(const std::string& operation_id, const std::string& approver_id);
    bool RejectOperation(const std::string& operation_id, const std::string& reason);

    // Compliance and auditing
    ComplianceReport GenerateComplianceReport(const std::string& start_date = "", const std::string& end_date = "") const;
    std::vector<OperationMetadata> GetOperatorHistory(const std::string& operator_id, size_t limit = 100) const;
    std::vector<std::string> GetUnauthorizedOperations() const;
    bool DetectAnomalousBehavior(const std::string& operator_id) const;

    // Configuration
    void SetValidationLevel(ValidationLevel level);
    void SetOperatorRoleRequirements(const std::map<OperatorPurpose, std::vector<OperatorRole>>& requirements);
    void AddRestrictedEnvironment(const std::string& environment, const std::vector<OperatorRole>& allowed_roles);
    void EnableStrictMode(bool enabled);
    void SetApprovalRequired(const std::vector<OperatorPurpose>& purposes);

    // Security and access control
    bool CheckPermission(const std::string& operator_id, const std::string& permission) const;
    bool ValidateIPAddress(const std::string& ip_address, const std::string& operator_id) const;
    bool ValidateSessionDuration(const std::string& session_id, std::chrono::hours max_duration) const;
    std::vector<std::string> GetSuspiciousActivities(const std::string& operator_id) const;

    // Utilities
    std::string GenerateOperationId() const;
    std::string GenerateSessionId(const std::string& operator_id) const;
    OperatorPurpose StringToPurpose(const std::string& purpose_str) const;
    std::string PurposeToString(const OperatorPurpose purpose) const;
    OperatorRole StringToRole(const std::string& role_str) const;
    std::string RoleToString(const OperatorRole role) const;

private:
    ValidationLevel default_validation_level_;
    bool strict_mode_enabled_;

    mutable std::mutex validator_mutex_;

    // Operator database
    std::map<std::string, OperatorIdentifier> operators_;
    std::map<std::string, std::vector<OperationMetadata>> operator_history_;
    std::map<std::string, OperationMetadata> active_operations_;

    // Configuration
    std::map<OperatorPurpose, std::vector<OperatorRole>> purpose_role_requirements_;
    std::map<std::string, std::vector<OperatorRole>> environment_restrictions_;
    std::set<OperatorPurpose> approval_required_purposes_;

    // Validation rules
    std::vector<std::function<bool(const std::string&)>> operator_id_validators_;
    std::vector<std::function<bool(const OperationMetadata&)>> operation_validators_;

    // Internal methods
    bool IsValidOperatorIdFormat(const std::string& operator_id) const;
    bool IsOperatorActive(const std::string& operator_id) const;
    bool HasRequiredRole(const std::string& operator_id, OperatorPurpose purpose) const;
    bool CanAccessEnvironment(const std::string& operator_id, const std::string& environment) const;
    bool RequiresApproval(const OperationMetadata& metadata) const;

    std::vector<std::string> ValidateOperatorMetadataCompleteness(const OperationMetadata& metadata) const;
    std::vector<std::string> ValidateOperationTiming(const OperationMetadata& metadata) const;
    std::vector<std::string> ValidateSecurityConstraints(const OperationMetadata& metadata) const;

    double CalculateComplianceScore(const ComplianceReport& report) const;
    std::vector<std::string> IdentifyComplianceViolations(const ComplianceReport& report) const;

    // Serialization helpers
    json OperationMetadataToJson(const OperationMetadata& metadata) const;
    json ValidationResultToJson(const ValidationResult& result) const;
    json ComplianceReportToJson(const ComplianceReport& report) const;
    OperationMetadata JsonToOperationMetadata(const json& j) const;
    ValidationResult JsonToValidationResult(const json& j) const;
};

/**
 * @brief Scoped operation validator for automatic metadata validation
 */
class ScopedOperationValidator {
public:
    ScopedOperationValidator(OperatorMetadataValidator& validator,
                           const OperationMetadata& metadata);
    ~ScopedOperationValidator();

    bool IsValid() const;
    const ValidationResult& GetValidationResult() const;
    void SetOperationResult(const json& result_summary);

private:
    OperatorMetadataValidator& validator_;
    OperationMetadata metadata_;
    ValidationResult validation_result_;
    bool operation_completed_;
};

/**
 * @brief Utility class for operator metadata templates
 */
class OperatorMetadataTemplates {
public:
    static OperationMetadata CreateSystemMaintenanceTemplate(const std::string& operator_id);
    static OperationMetadata CreatePerformanceAnalysisTemplate(const std::string& operator_id);
    static OperationMetadata CreateDeploymentTemplate(const std::string& operator_id);
    static OperationMetadata CreateDataProcessingTemplate(const std::string& operator_id);
    static OperationMetadata CreateDebuggingTemplate(const std::string& operator_id);
    static OperationMetadata CreateTestingTemplate(const std::string& operator_id);
    static OperationMetadata CreateMonitoringTemplate(const std::string& operator_id);
    static OperationMetadata CreateSecurityAuditTemplate(const std::string& operator_id);

private:
    static OperationMetadata CreateBaseTemplate(const std::string& operator_id, OperatorPurpose purpose);
};

/**
 * @brief Factory function to create and configure operator metadata validator
 */
std::unique_ptr<OperatorMetadataValidator> CreateOperatorMetadataValidator(
    ValidationLevel default_level = ValidationLevel::STRICT,
    bool strict_mode = true);

} // namespace puzzle71::services