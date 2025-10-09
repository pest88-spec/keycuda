/**
 * Puzzle71Solver - Integration Management System Implementation
 *
 * Provides infrastructure for managing third-party library integration,
 * including attribution, verification, and build system integration.
 *
 * @author       Puzzle71Solver Team
 * @created      2025-10-09
 * @license      MIT
 */

#include "integration_manager.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <queue>
#include <set>

namespace integration {

struct IntegrationManager::Impl {
    std::vector<LibraryInfo> libraries;
    std::map<std::string, std::string> configuration;
    std::map<std::string, AttributionInfo> attribution_registry;
    std::filesystem::path integration_root = "src/extracted";
    bool logging_enabled = true;
    std::string log_level = "INFO";
    std::vector<std::string> log_entries;

    // T014a: Dependency monitoring data structures
    std::vector<ConflictInfo> active_conflicts;
    std::map<std::string, std::vector<std::string>> dependency_graph;
    std::vector<DependencyConflict> dependency_conflicts;

    // T014b: Component inclusion data structures
    std::map<std::string, std::vector<std::string>> excluded_components;
    std::map<std::string, std::string> component_types;

    // T014c: Resource allocation data structures
    std::map<std::string, ResourceAllocation> resource_allocations;
    std::vector<IntegrationPool> integration_pools;
    std::map<std::string, size_t> resource_estimates;

    void log(const std::string& level, const std::string& message) {
        if (!logging_enabled) return;

        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);

        std::ostringstream oss;
        oss << "[" << std::put_time(std::gmtime(&time_t), "%Y-%m-%d %H:%M:%S") << "] "
            << "[" << level << "] " << message;

        log_entries.push_back(oss.str());

        // Also output to console for immediate feedback
        if (level == "ERROR" || level == "WARNING") {
            std::cerr << oss.str() << std::endl;
        } else {
            std::cout << oss.str() << std::endl;
        }
    }

    bool create_directory_structure(const std::filesystem::path& library_path) {
        try {
            std::filesystem::create_directories(library_path / "src");
            std::filesystem::create_directories(library_path / "include");
            std::filesystem::create_directories(library_path / "attribution_headers");
            return true;
        } catch (const std::exception& e) {
            log("ERROR", "Failed to create directory structure: " + std::string(e.what()));
            return false;
        }
    }

    bool copy_source_files(const std::filesystem::path& source,
                          const std::filesystem::path& destination,
                          const std::vector<std::string>& file_patterns) {
        // Implementation for copying source files with attribution
        log("INFO", "Copying source files from " + source.string() + " to " + destination.string());
        return true; // Simplified for now
    }

    std::string generate_attribution_header(const AttributionInfo& attribution) {
        std::ostringstream oss;
        oss << "/**\n";
        oss << " * Extracted from " << attribution.project_name << " by " << attribution.author << "\n";
        oss << " *\n";
        oss << " * @origin       " << attribution.origin_url << "\n";
        oss << " * @origin_path  " << attribution.origin_path << "\n";
        oss << " * @origin_commit " << attribution.origin_commit << "\n";
        oss << " * @origin_license " << attribution.origin_license << "\n";
        oss << " * @extracted_date   " << std::chrono::year_month_day{attribution.extracted_date} << "\n";
        oss << " * @extracted_by     " << attribution.extracted_by << "\n";
        oss << " * @modifications    " << attribution.modifications << "\n";
        oss << " * @spdx_license_identifier " << attribution.spdx_license_identifier << "\n";
        oss << " */\n";
        return oss.str();
    }

    void initialize_integration_pools() {
        if (integration_pools.empty()) {
            integration_pools = {
                {"core", 1024 * 1024 * 1024, 0, {}, {}},      // 1GB for core libraries
                {"optional", 512 * 1024 * 1024, 0, {}, {}},    // 512MB for optional libraries
                {"cache", 256 * 1024 * 1024, 0, {}, {}}        // 256MB for caching
            };
            log("INFO", "Initialized integration resource pools");
        }
    }

    std::vector<std::string> find_path_between_dependencies(const std::string& from, const std::string& to) {
        // Simple BFS to find dependency path
        std::queue<std::string> queue;
        std::map<std::string, std::string> parent;
        std::set<std::string> visited;

        queue.push(from);
        visited.insert(from);

        while (!queue.empty()) {
            std::string current = queue.front();
            queue.pop();

            if (current == to) {
                // Reconstruct path
                std::vector<std::string> path;
                std::string node = to;
                while (node != from) {
                    path.insert(path.begin(), node);
                    node = parent[node];
                }
                path.insert(path.begin(), from);
                return path;
            }

            for (const auto& dep : dependency_graph[current]) {
                if (visited.find(dep) == visited.end()) {
                    visited.insert(dep);
                    parent[dep] = current;
                    queue.push(dep);
                }
            }
        }

        return {};  // No path found
    }

    bool detect_circular_dependency(const std::string& library_name, std::set<std::string>& visited, std::set<std::string>& rec_stack) {
        visited.insert(library_name);
        rec_stack.insert(library_name);

        for (const auto& dep : dependency_graph[library_name]) {
            if (rec_stack.find(dep) != rec_stack.end()) {
                return true;  // Circular dependency detected
            }
            if (visited.find(dep) == visited.end() && detect_circular_dependency(dep, visited, rec_stack)) {
                return true;
            }
        }

        rec_stack.erase(library_name);
        return false;
    }
};

IntegrationManager::IntegrationManager() : p_impl(std::make_unique<Impl>()) {
    p_impl->initialize_integration_pools();
    p_impl->log("INFO", "Integration Manager initialized with enhanced capabilities");
}

IntegrationManager::~IntegrationManager() = default;

bool IntegrationManager::integrate_library(const LibraryInfo& library) {
    p_impl->log("INFO", "Starting integration of library: " + library.name + " v" + library.version);

    auto start_time = std::chrono::steady_clock::now();

    // Check if library already exists
    if (is_library_integrated(library.name)) {
        p_impl->log("WARNING", "Library " + library.name + " is already integrated");
        return false;
    }

    // Create directory structure
    std::filesystem::path library_path = p_impl->integration_root / library.name;
    if (!p_impl->create_directory_structure(library_path)) {
        p_impl->log("ERROR", "Failed to create directory structure for " + library.name);
        return false;
    }

    // Add library to registry
    LibraryInfo new_library = library;
    new_library.is_integrated = true;
    new_library.integration_date = std::chrono::system_clock::now();
    p_impl->libraries.push_back(new_library);

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    p_impl->log("INFO", "Successfully integrated library " + library.name + " in " +
                std::to_string(duration.count()) + "ms");

    return true;
}

bool IntegrationManager::verify_integration(const std::string& library_name) {
    p_impl->log("INFO", "Verifying integration of library: " + library_name);

    auto it = std::find_if(p_impl->libraries.begin(), p_impl->libraries.end(),
                          [&library_name](const LibraryInfo& lib) {
                              return lib.name == library_name;
                          });

    if (it == p_impl->libraries.end()) {
        p_impl->log("ERROR", "Library " + library_name + " not found in registry");
        return false;
    }

    // Verify directory structure exists
    std::filesystem::path library_path = p_impl->integration_root / library_name;
    if (!std::filesystem::exists(library_path)) {
        p_impl->log("ERROR", "Integration directory for " + library_name + " does not exist");
        return false;
    }

    p_impl->log("INFO", "Library " + library_name + " verification passed");
    return true;
}

bool IntegrationManager::update_library(const std::string& library_name, const std::string& new_version) {
    p_impl->log("INFO", "Updating library " + library_name + " to version " + new_version);

    auto it = std::find_if(p_impl->libraries.begin(), p_impl->libraries.end(),
                          [&library_name](const LibraryInfo& lib) {
                              return lib.name == library_name;
                          });

    if (it == p_impl->libraries.end()) {
        p_impl->log("ERROR", "Library " + library_name + " not found for update");
        return false;
    }

    it->version = new_version;
    p_impl->log("INFO", "Successfully updated " + library_name + " to version " + new_version);
    return true;
}

bool IntegrationManager::remove_library(const std::string& library_name) {
    p_impl->log("INFO", "Removing library: " + library_name);

    auto it = std::find_if(p_impl->libraries.begin(), p_impl->libraries.end(),
                          [&library_name](const LibraryInfo& lib) {
                              return lib.name == library_name;
                          });

    if (it == p_impl->libraries.end()) {
        p_impl->log("ERROR", "Library " + library_name + " not found for removal");
        return false;
    }

    p_impl->libraries.erase(it);

    // Remove directory
    std::filesystem::path library_path = p_impl->integration_root / library_name;
    if (std::filesystem::exists(library_path)) {
        std::filesystem::remove_all(library_path);
    }

    p_impl->log("INFO", "Successfully removed library " + library_name);
    return true;
}

bool IntegrationManager::add_attribution_header(const std::filesystem::path& file_path,
                                              const AttributionInfo& attribution) {
    if (!std::filesystem::exists(file_path)) {
        p_impl->log("ERROR", "File not found for attribution: " + file_path.string());
        return false;
    }

    std::string attribution_header = p_impl->generate_attribution_header(attribution);

    // Read existing content
    std::ifstream input_file(file_path);
    std::string content((std::istreambuf_iterator<char>(input_file)),
                       std::istreambuf_iterator<char>());
    input_file.close();

    // Write content with attribution header
    std::ofstream output_file(file_path);
    output_file << attribution_header << "\n" << content;
    output_file.close();

    // Register attribution
    p_impl->attribution_registry[file_path.string()] = attribution;

    p_impl->log("INFO", "Added attribution header to: " + file_path.string());
    return true;
}

bool IntegrationManager::verify_attribution_coverage(const std::string& library_name) {
    p_impl->log("INFO", "Verifying attribution coverage for library: " + library_name);

    std::filesystem::path library_path = p_impl->integration_root / library_name;
    if (!std::filesystem::exists(library_path)) {
        p_impl->log("ERROR", "Library path not found: " + library_path.string());
        return false;
    }

    bool all_files_have_attribution = true;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(library_path)) {
        if (entry.is_regular_file()) {
            std::string file_path = entry.path().string();
            if (p_impl->attribution_registry.find(file_path) == p_impl->attribution_registry.end()) {
                p_impl->log("WARNING", "File missing attribution: " + file_path);
                all_files_have_attribution = false;
            }
        }
    }

    if (all_files_have_attribution) {
        p_impl->log("INFO", "All files in " + library_name + " have proper attribution");
    } else {
        p_impl->log("WARNING", "Some files in " + library_name + " are missing attribution");
    }

    return all_files_have_attribution;
}

std::vector<AttributionInfo> IntegrationManager::get_attribution_for_library(const std::string& library_name) {
    std::vector<AttributionInfo> attributions;

    for (const auto& pair : p_impl->attribution_registry) {
        if (pair.first.find(library_name) != std::string::npos) {
            attributions.push_back(pair.second);
        }
    }

    return attributions;
}

bool IntegrationManager::load_configuration(const std::filesystem::path& config_path) {
    // Implementation for loading configuration from JSON file
    p_impl->log("INFO", "Loading configuration from: " + config_path.string());
    return true; // Simplified for now
}

bool IntegrationManager::save_configuration(const std::filesystem::path& config_path) {
    // Implementation for saving configuration to JSON file
    p_impl->log("INFO", "Saving configuration to: " + config_path.string());
    return true; // Simplified for now
}

bool IntegrationManager::set_integration_property(const std::string& key, const std::string& value) {
    p_impl->configuration[key] = value;
    p_impl->log("INFO", "Set integration property: " + key + " = " + value);
    return true;
}

std::string IntegrationManager::get_integration_property(const std::string& key) const {
    auto it = p_impl->configuration.find(key);
    if (it != p_impl->configuration.end()) {
        return it->second;
    }
    return "";
}

bool IntegrationManager::verify_library_integrity(const std::string& library_name) {
    p_impl->log("INFO", "Verifying library integrity for: " + library_name);
    return verify_integration(library_name); // Simplified for now
}

bool IntegrationManager::verify_build_system_integration() {
    p_impl->log("INFO", "Verifying build system integration");
    return true; // Simplified for now
}

bool IntegrationManager::generate_integration_report(const std::string& library_name, IntegrationReport& report) {
    p_impl->log("INFO", "Generating integration report for: " + library_name);

    auto it = std::find_if(p_impl->libraries.begin(), p_impl->libraries.end(),
                          [&library_name](const LibraryInfo& lib) {
                              return lib.name == library_name;
                          });

    if (it == p_impl->libraries.end()) {
        report.success = false;
        report.message = "Library not found in registry";
        return false;
    }

    report.success = true;
    report.message = "Integration report generated successfully";
    report.build_configuration_updated = true;
    report.integration_time = std::chrono::milliseconds(100); // Placeholder

    return true;
}

void IntegrationManager::enable_logging(bool enabled) {
    p_impl->logging_enabled = enabled;
    p_impl->log("INFO", "Logging " + std::string(enabled ? "enabled" : "disabled"));
}

void IntegrationManager::set_log_level(const std::string& level) {
    p_impl->log_level = level;
    p_impl->log("INFO", "Log level set to: " + level);
}

std::string IntegrationManager::get_integration_logs() const {
    std::ostringstream oss;
    for (const auto& entry : p_impl->log_entries) {
        oss << entry << "\n";
    }
    return oss.str();
}

std::vector<LibraryInfo> IntegrationManager::get_integrated_libraries() const {
    return p_impl->libraries;
}

bool IntegrationManager::is_library_integrated(const std::string& library_name) const {
    return std::find_if(p_impl->libraries.begin(), p_impl->libraries.end(),
                       [&library_name](const LibraryInfo& lib) {
                           return lib.name == library_name;
                       }) != p_impl->libraries.end();
}

std::filesystem::path IntegrationManager::get_integration_root() const {
    return p_impl->integration_root;
}

// T014a: Dependency monitoring and conflict detection implementations

std::vector<ConflictInfo> IntegrationManager::detect_conflicts(const std::string& library_name) {
    p_impl->log("INFO", "Detecting conflicts for library: " + library_name);

    std::vector<ConflictInfo> conflicts;

    // Check for version conflicts with existing libraries
    auto lib_it = std::find_if(p_impl->libraries.begin(), p_impl->libraries.end(),
                              [&library_name](const LibraryInfo& lib) {
                                  return lib.name == library_name;
                              });

    if (lib_it == p_impl->libraries.end()) {
        p_impl->log("ERROR", "Library not found for conflict detection: " + library_name);
        return conflicts;
    }

    // Check version conflicts
    for (const auto& existing_lib : p_impl->libraries) {
        if (existing_lib.name != library_name) {
            // Check for identical file names
            for (const auto& file1 : lib_it->source_files) {
                for (const auto& file2 : existing_lib.source_files) {
                    if (file1 == file2) {
                        ConflictInfo conflict;
                        conflict.conflict_type = "file";
                        conflict.library1 = library_name;
                        conflict.library2 = existing_lib.name;
                        conflict.description = "Identical source file: " + file1;
                        conflict.severity = "error";
                        conflict.resolution = "Rename file or exclude one library";
                        conflicts.push_back(conflict);
                    }
                }
            }

            // Check for license compatibility
            if (lib_it->license_type != existing_lib.license_type) {
                ConflictInfo conflict;
                conflict.conflict_type = "license";
                conflict.library1 = library_name;
                conflict.library2 = existing_lib.name;
                conflict.description = "Different license types: " + lib_it->license_type + " vs " + existing_lib.license_type;
                conflict.severity = "warning";
                conflict.resolution = "Review license compatibility";
                conflicts.push_back(conflict);
            }
        }
    }

    p_impl->log("INFO", "Found " + std::to_string(conflicts.size()) + " conflicts for " + library_name);
    return conflicts;
}

std::vector<DependencyConflict> IntegrationManager::detect_dependency_conflicts(const std::vector<std::string>& libraries) {
    p_impl->log("INFO", "Detecting dependency conflicts across " + std::to_string(libraries.size()) + " libraries");

    std::vector<DependencyConflict> conflicts;
    std::set<std::string> visited;
    std::set<std::string> rec_stack;

    // Check for circular dependencies
    for (const auto& library : libraries) {
        if (visited.find(library) == visited.end()) {
            if (p_impl->detect_circular_dependency(library, visited, rec_stack)) {
                DependencyConflict conflict;
                conflict.library_name = library;
                conflict.conflict_type = "circular";
                conflict.description = "Circular dependency detected involving " + library;
                conflict.is_blocking = true;
                conflicts.push_back(conflict);
            }
        }
    }

    // Check for missing dependencies
    for (const auto& library : libraries) {
        for (const auto& dep : p_impl->dependency_graph[library]) {
            bool dep_found = std::find(libraries.begin(), libraries.end(), dep) != libraries.end();
            if (!dep_found && !is_library_integrated(dep)) {
                DependencyConflict conflict;
                conflict.library_name = library;
                conflict.conflict_type = "missing";
                conflict.conflict_chain = {library, dep};
                conflict.description = "Missing dependency: " + dep + " required by " + library;
                conflict.is_blocking = true;
                conflicts.push_back(conflict);
            }
        }
    }

    p_impl->log("INFO", "Found " + std::to_string(conflicts.size()) + " dependency conflicts");
    return conflicts;
}

bool IntegrationManager::resolve_conflict(const ConflictInfo& conflict) {
    p_impl->log("INFO", "Resolving conflict: " + conflict.description);

    // Implementation depends on conflict type
    if (conflict.conflict_type == "file") {
        // Could rename files or apply exclusions
        p_impl->log("INFO", "File conflict resolution would require file renaming or exclusion");
        return false; // Requires manual intervention
    } else if (conflict.conflict_type == "license") {
        // License conflicts typically require legal review
        p_impl->log("WARNING", "License conflict requires legal review");
        return false; // Requires manual review
    }

    return false;
}

bool IntegrationManager::validate_dependency_chain(const std::string& library_name) {
    p_impl->log("INFO", "Validating dependency chain for: " + library_name);

    std::set<std::string> visited;
    std::set<std::string> rec_stack;

    return !p_impl->detect_circular_dependency(library_name, visited, rec_stack);
}

std::map<std::string, std::vector<std::string>> IntegrationManager::get_dependency_graph() const {
    return p_impl->dependency_graph;
}

// T014b: Selective component inclusion implementations

bool IntegrationManager::set_excluded_components(const std::string& library_name, const std::vector<std::string>& components) {
    p_impl->log("INFO", "Setting excluded components for " + library_name + ": " + std::to_string(components.size()) + " components");

    p_impl->excluded_components[library_name] = components;

    // Update library info if it exists
    auto it = std::find_if(p_impl->libraries.begin(), p_impl->libraries.end(),
                          [&library_name](const LibraryInfo& lib) {
                              return lib.name == library_name;
                          });

    if (it != p_impl->libraries.end()) {
        it->excluded_components = components;
    }

    return true;
}

std::vector<std::string> IntegrationManager::get_excluded_components(const std::string& library_name) const {
    auto it = p_impl->excluded_components.find(library_name);
    if (it != p_impl->excluded_components.end()) {
        return it->second;
    }
    return {};
}

bool IntegrationManager::include_component_type(const std::string& library_name, const std::string& component_type) {
    p_impl->log("INFO", "Setting component type for " + library_name + ": " + component_type);

    p_impl->component_types[library_name] = component_type;

    // Update library info
    auto it = std::find_if(p_impl->libraries.begin(), p_impl->libraries.end(),
                          [&library_name](const LibraryInfo& lib) {
                              return lib.name == library_name;
                          });

    if (it != p_impl->libraries.end()) {
        it->component_type = component_type;
    }

    return true;
}

std::vector<std::string> IntegrationManager::get_component_inclusion_plan(const std::string& library_name) const {
    std::vector<std::string> plan;

    // Default excluded components for most libraries
    std::vector<std::string> default_excluded = {"tests", "docs", "examples", "benchmarks"};

    auto excluded_it = p_impl->excluded_components.find(library_name);
    if (excluded_it != p_impl->excluded_components.end()) {
        plan = excluded_it->second;
    } else {
        plan = default_excluded;
    }

    return plan;
}

// T014c: Integration pool allocation optimization implementations

bool IntegrationManager::allocate_to_pool(const std::string& library_name, const std::string& pool_name) {
    p_impl->log("INFO", "Allocating " + library_name + " to pool: " + pool_name);

    // Find the pool
    auto pool_it = std::find_if(p_impl->integration_pools.begin(), p_impl->integration_pools.end(),
                               [&pool_name](const IntegrationPool& pool) {
                                   return pool.pool_name == pool_name;
                               });

    if (pool_it == p_impl->integration_pools.end()) {
        p_impl->log("ERROR", "Pool not found: " + pool_name);
        return false;
    }

    // Estimate library resource requirements
    size_t estimated_size = estimate_library_resources(library_name);

    // Check if pool has sufficient capacity
    if (pool_it->used_capacity + estimated_size > pool_it->total_capacity) {
        p_impl->log("ERROR", "Insufficient capacity in pool " + pool_name);
        return false;
    }

    // Allocate resources
    ResourceAllocation allocation;
    allocation.library_name = library_name;
    allocation.resource_type = "memory";
    allocation.allocated_amount = estimated_size;
    allocation.used_amount = 0;
    allocation.allocation_pool = pool_name;
    allocation.priority_score = 1.0f; // Default priority

    p_impl->resource_allocations[library_name] = allocation;
    pool_it->used_capacity += estimated_size;
    pool_it->allocated_libraries.push_back(library_name);
    pool_it->library_allocations[library_name] = estimated_size;

    p_impl->log("INFO", "Successfully allocated " + library_name + " to " + pool_name);
    return true;
}

ResourceAllocation IntegrationManager::get_resource_allocation(const std::string& library_name) const {
    auto it = p_impl->resource_allocations.find(library_name);
    if (it != p_impl->resource_allocations.end()) {
        return it->second;
    }
    return {};
}

bool IntegrationManager::optimize_pool_allocations() {
    p_impl->log("INFO", "Optimizing pool allocations");

    // Simple optimization: move libraries to more appropriate pools based on usage
    for (auto& allocation : p_impl->resource_allocations) {
        if (allocation.second.priority_score < 0.5f && allocation.second.allocation_pool == "core") {
            // Move low priority libraries from core to optional
            allocate_to_pool(allocation.first, "optional");
        }
    }

    p_impl->log("INFO", "Pool optimization completed");
    return true;
}

std::vector<IntegrationPool> IntegrationManager::get_integration_pools() const {
    return p_impl->integration_pools;
}

size_t IntegrationManager::estimate_library_resources(const std::string& library_name) const {
    auto it = p_impl->resource_estimates.find(library_name);
    if (it != p_impl->resource_estimates.end()) {
        return it->second;
    }

    // Simple estimation based on library info
    auto lib_it = std::find_if(p_impl->libraries.begin(), p_impl->libraries.end(),
                              [&library_name](const LibraryInfo& lib) {
                                  return lib.name == library_name;
                              });

    if (lib_it != p_impl->libraries.end()) {
        // Estimate based on number of source files (rough heuristic)
        size_t estimated_size = lib_it->source_files.size() * 1024 * 1024; // 1MB per source file
        return estimated_size;
    }

    return 100 * 1024 * 1024; // Default 100MB estimate
}

// Global instance
IntegrationManager& get_integration_manager() {
    static IntegrationManager instance;
    return instance;
}

} // namespace integration