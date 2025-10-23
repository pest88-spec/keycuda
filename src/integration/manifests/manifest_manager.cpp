// T019: Integration Manifest Implementation
// Manages build configuration manifests for integrated dependencies

#include "manifest_manager.h"
#include <fstream>
#include <sstream>
#include <iostream>

namespace integration {
namespace manifests {

ManifestManager::ManifestManager(const std::string& output_dir)
    : output_dir_(output_dir) {
    load_manifest();
}

void ManifestManager::add_dependency(const DependencyInfo& dep) {
    dependencies_[dep.name] = dep;
    manifest_dirty_ = true;
}

void ManifestManager::remove_dependency(const std::string& name) {
    dependencies_.erase(name);
    manifest_dirty_ = true;
}

void ManifestManager::update_dependency_status(const std::string& name,
                                             IntegrationStatus status) {
    auto it = dependencies_.find(name);
    if (it != dependencies_.end()) {
        it->second.status = status;
        it->second.last_updated = get_current_timestamp();
        manifest_dirty_ = true;
    }
}

bool ManifestManager::has_dependency(const std::string& name) const {
    return dependencies_.find(name) != dependencies_.end();
}

DependencyInfo ManifestManager::get_dependency(const std::string& name) const {
    auto it = dependencies_.find(name);
    if (it != dependencies_.end()) {
        return it->second;
    }
    return {};
}

std::vector<DependencyInfo> ManifestManager::get_all_dependencies() const {
    std::vector<DependencyInfo> result;
    for (const auto& pair : dependencies_) {
        result.push_back(pair.second);
    }
    return result;
}

bool ManifestManager::validate_dependencies() const {
    bool all_valid = true;

    for (const auto& pair : dependencies_) {
        const auto& dep = pair.second;

        // Check required fields
        if (dep.name.empty() || dep.version.empty() || dep.source_path.empty()) {
            std::cerr << "Dependency " << dep.name << " missing required fields\n";
            all_valid = false;
        }

        // Check SHA-256 if provided
        if (!dep.sha256.empty() && dep.sha256.length() != 64) {
            std::cerr << "Dependency " << dep.name << " has invalid SHA-256\n";
            all_valid = false;
        }

        // Check status
        if (dep.status == IntegrationStatus::FAILED) {
            std::cerr << "Dependency " << dep.name << " integration failed\n";
            all_valid = false;
        }
    }

    return all_valid;
}

void ManifestManager::save_manifest() const {
    if (!manifest_dirty_) {
        return;
    }

    nlohmann::json manifest;
    manifest["version"] = "1.0.0";
    manifest["generated"] = get_current_timestamp();
    manifest["dependencies"] = nlohmann::json::array();

    for (const auto& pair : dependencies_) {
        const auto& dep = pair.second;
        nlohmann::json dep_json;
        dep_json["name"] = dep.name;
        dep_json["version"] = dep.version;
        dep_json["source_url"] = dep.source_url;
        dep_json["source_path"] = dep.source_path;
        dep_json["integrated_path"] = dep.integrated_path;
        dep_json["sha256"] = dep.sha256;
        dep_json["license"] = dep.license;
        dep_json["attribution"] = dep.attribution;
        dep_json["status"] = status_to_string(dep.status);
        dep_json["last_updated"] = dep.last_updated;
        dep_json["notes"] = dep.notes;

        manifest["dependencies"].push_back(dep_json);
    }

    std::string filename = output_dir_ + "/integration-manifest.json";
    std::ofstream file(filename);
    if (file.is_open()) {
        file << manifest.dump(2) << std::endl;
        file.close();
    }
}

void ManifestManager::load_manifest() {
    std::string filename = output_dir_ + "/integration-manifest.json";
    std::ifstream file(filename);

    if (!file.is_open()) {
        // File doesn't exist, create empty manifest
        dependencies_.clear();
        return;
    }

    try {
        nlohmann::json manifest;
        file >> manifest;

        if (manifest.contains("dependencies")) {
            for (const auto& dep_json : manifest["dependencies"]) {
                DependencyInfo dep;
                dep.name = dep_json.value("name", "");
                dep.version = dep_json.value("version", "");
                dep.source_url = dep_json.value("source_url", "");
                dep.source_path = dep_json.value("source_path", "");
                dep.integrated_path = dep_json.value("integrated_path", "");
                dep.sha256 = dep_json.value("sha256", "");
                dep.license = dep_json.value("license", "");
                dep.attribution = dep_json.value("attribution", "");
                dep.status = string_to_status(dep_json.value("status", ""));
                dep.last_updated = dep_json.value("last_updated", "");
                dep.notes = dep_json.value("notes", "");

                if (!dep.name.empty()) {
                    dependencies_[dep.name] = dep;
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error loading manifest: " << e.what() << std::endl;
        dependencies_.clear();
    }

    manifest_dirty_ = false;
}

std::string ManifestManager::status_to_string(IntegrationStatus status) const {
    switch (status) {
        case IntegrationStatus::PENDING: return "pending";
        case IntegrationStatus::IN_PROGRESS: return "in_progress";
        case IntegrationStatus::COMPLETED: return "completed";
        case IntegrationStatus::FAILED: return "failed";
        case IntegrationStatus::SKIPPED: return "skipped";
        default: return "unknown";
    }
}

IntegrationStatus ManifestManager::string_to_status(const std::string& status_str) const {
    if (status_str == "pending") return IntegrationStatus::PENDING;
    if (status_str == "in_progress") return IntegrationStatus::IN_PROGRESS;
    if (status_str == "completed") return IntegrationStatus::COMPLETED;
    if (status_str == "failed") return IntegrationStatus::FAILED;
    if (status_str == "skipped") return IntegrationStatus::SKIPPED;
    return IntegrationStatus::UNKNOWN;
}

std::string ManifestManager::get_current_timestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

} // namespace manifests
} // namespace integration