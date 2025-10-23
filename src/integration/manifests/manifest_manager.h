// T019: Integration Manifest Implementation
// Manages build configuration manifests for integrated dependencies

#pragma once

#include <string>
#include <map>
#include <vector>
#include <chrono>
#include <nlohmann/json.hpp>

namespace integration {
namespace manifests {

enum class IntegrationStatus {
    UNKNOWN = 0,
    PENDING,
    IN_PROGRESS,
    COMPLETED,
    FAILED,
    SKIPPED
};

struct DependencyInfo {
    std::string name;
    std::string version;
    std::string source_url;
    std::string source_path;
    std::string integrated_path;
    std::string sha256;
    std::string license;
    std::string attribution;
    IntegrationStatus status = IntegrationStatus::PENDING;
    std::string last_updated;
    std::string notes;
};

class ManifestManager {
public:
    explicit ManifestManager(const std::string& output_dir);

    // Dependency management
    void add_dependency(const DependencyInfo& dep);
    void remove_dependency(const std::string& name);
    void update_dependency_status(const std::string& name, IntegrationStatus status);

    // Query operations
    bool has_dependency(const std::string& name) const;
    DependencyInfo get_dependency(const std::string& name) const;
    std::vector<DependencyInfo> get_all_dependencies() const;

    // Validation
    bool validate_dependencies() const;

    // Persistence
    void save_manifest() const;
    void load_manifest();

    // Configuration
    void set_output_directory(const std::string& dir) { output_dir_ = dir; }
    const std::string& get_output_directory() const { return output_dir_; }

private:
    std::string status_to_string(IntegrationStatus status) const;
    IntegrationStatus string_to_status(const std::string& status_str) const;
    std::string get_current_timestamp() const;

    std::string output_dir_;
    std::map<std::string, DependencyInfo> dependencies_;
    mutable bool manifest_dirty_ = true;
};

} // namespace manifests
} // namespace integration