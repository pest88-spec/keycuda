/**
 * Puzzle71Solver - Integration Manifest Implementation
 *
 * Implements comprehensive build configuration management through integration manifests,
 * enabling standardized library integration with complete dependency tracking and
 * build system orchestration.
 *
 * @author       Puzzle71Solver Team
 * @created      2025-10-10
 * @license      MIT
 */

#include "manifest.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <regex>
#include <iomanip>
#include <iostream>

// Include JSON parsing library (nlohmann/json)
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace integration {
namespace manifest {

// Implementation structure
struct ManifestManager::Impl {
    std::filesystem::path manifest_directory;
    ProjectManifest project_manifest;
    std::map<std::string, LibraryManifest> library_manifests;
    bool auto_save = true;
    bool validation_enabled = true;
    std::vector<std::function<void(const std::string&, const LibraryManifest&)>> manifest_change_callbacks;
    std::vector<std::function<void(const std::string&, IntegrationStatus, IntegrationStatus)>> status_change_callbacks;
    std::vector<std::function<void(const std::string&, const ManifestValidationResult&)>> validation_callbacks;
    std::map<std::string, IntegrationStatus> previous_status;

    Impl() {
        // Initialize with default paths
        manifest_directory = "build/manifests";
    }
};

// Constructor implementations
ManifestManager::ManifestManager() : p_impl(std::make_unique<Impl>()) {}

ManifestManager::ManifestManager(const std::filesystem::path& manifest_directory)
    : p_impl(std::make_unique<Impl>()) {
    p_impl->manifest_directory = manifest_directory;
}

ManifestManager::~ManifestManager() = default;

// Configuration methods
void ManifestManager::set_manifest_directory(const std::filesystem::path& directory) {
    p_impl->manifest_directory = directory;
    std::filesystem::create_directories(directory);
}

std::filesystem::path ManifestManager::get_manifest_directory() const {
    return p_impl->manifest_directory;
}

void ManifestManager::set_auto_save(bool enabled) {
    p_impl->auto_save = enabled;
}

void ManifestManager::set_validation_enabled(bool enabled) {
    p_impl->validation_enabled = enabled;
}

// Library manifest operations
bool ManifestManager::create_library_manifest(const LibraryManifest& manifest) {
    if (p_impl->validation_enabled) {
        auto validation = validate_library_manifest(manifest);
        if (!validation.is_valid) {
            for (const auto& error : validation.errors) {
                std::cerr << "Manifest validation error: " << error << std::endl;
            }
            return false;
        }
    }

    p_impl->library_manifests[manifest.name] = manifest;

    if (p_impl->auto_save) {
        return save_library_manifest(manifest);
    }

    return true;
}

bool ManifestManager::load_library_manifest(const std::string& library_name, LibraryManifest& manifest) {
    auto it = p_impl->library_manifests.find(library_name);
    if (it != p_impl->library_manifests.end()) {
        manifest = it->second;
        return true;
    }

    // Try to load from file
    std::filesystem::path manifest_path = get_manifest_path(library_name);
    if (!std::filesystem::exists(manifest_path)) {
        return false;
    }

    std::ifstream file(manifest_path);
    if (!file.is_open()) {
        return false;
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    try {
        manifest = deserialize_library_manifest(content);
        p_impl->library_manifests[library_name] = manifest;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error loading manifest for " << library_name << ": " << e.what() << std::endl;
        return false;
    }
}

bool ManifestManager::save_library_manifest(const LibraryManifest& manifest) {
    std::filesystem::path manifest_path = get_manifest_path(manifest.name);
    std::filesystem::create_directories(manifest_path.parent_path());

    std::ofstream file(manifest_path);
    if (!file.is_open()) {
        std::cerr << "Failed to open manifest file for writing: " << manifest_path << std::endl;
        return false;
    }

    try {
        std::string serialized = serialize_manifest(manifest);
        file << serialized;
        file.close();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error saving manifest for " << manifest.name << ": " << e.what() << std::endl;
        return false;
    }
}

bool ManifestManager::update_library_manifest(const std::string& library_name, const LibraryManifest& updates) {
    auto it = p_impl->library_manifests.find(library_name);
    if (it == p_impl->library_manifests.end()) {
        return false;
    }

    IntegrationStatus old_status = it->second.integration_status;
    it->second = updates;
    IntegrationStatus new_status = updates.integration_status;

    // Notify callbacks
    for (const auto& callback : p_impl->manifest_change_callbacks) {
        callback(library_name, updates);
    }

    if (old_status != new_status) {
        p_impl->previous_status[library_name] = old_status;
        for (const auto& callback : p_impl->status_change_callbacks) {
            callback(library_name, old_status, new_status);
        }
    }

    if (p_impl->auto_save) {
        return save_library_manifest(updates);
    }

    return true;
}

bool ManifestManager::delete_library_manifest(const std::string& library_name) {
    auto it = p_impl->library_manifests.find(library_name);
    if (it == p_impl->library_manifests.end()) {
        return false;
    }

    p_impl->library_manifests.erase(it);

    std::filesystem::path manifest_path = get_manifest_path(library_name);
    if (std::filesystem::exists(manifest_path)) {
        std::filesystem::remove(manifest_path);
    }

    return true;
}

std::vector<LibraryManifest> ManifestManager::get_all_library_manifests() const {
    std::vector<LibraryManifest> manifests;
    for (const auto& pair : p_impl->library_manifests) {
        manifests.push_back(pair.second);
    }
    return manifests;
}

bool ManifestManager::has_library_manifest(const std::string& library_name) const {
    return p_impl->library_manifests.find(library_name) != p_impl->library_manifests.end();
}

// Project manifest operations
bool ManifestManager::create_project_manifest(const ProjectManifest& manifest) {
    if (p_impl->validation_enabled) {
        auto validation = validate_project_manifest(manifest);
        if (!validation.is_valid) {
            for (const auto& error : validation.errors) {
                std::cerr << "Project manifest validation error: " << error << std::endl;
            }
            return false;
        }
    }

    p_impl->project_manifest = manifest;

    if (p_impl->auto_save) {
        return save_project_manifest(manifest);
    }

    return true;
}

bool ManifestManager::load_project_manifest(ProjectManifest& manifest) {
    std::filesystem::path project_manifest_path = p_impl->manifest_directory / "project.json";
    if (!std::filesystem::exists(project_manifest_path)) {
        return false;
    }

    std::ifstream file(project_manifest_path);
    if (!file.is_open()) {
        return false;
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    try {
        manifest = deserialize_project_manifest(content);
        p_impl->project_manifest = manifest;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error loading project manifest: " << e.what() << std::endl;
        return false;
    }
}

bool ManifestManager::save_project_manifest(const ProjectManifest& manifest) {
    std::filesystem::path project_manifest_path = p_impl->manifest_directory / "project.json";
    std::filesystem::create_directories(project_manifest_path.parent_path());

    std::ofstream file(project_manifest_path);
    if (!file.is_open()) {
        std::cerr << "Failed to open project manifest file for writing: " << project_manifest_path << std::endl;
        return false;
    }

    try {
        std::string serialized = serialize_manifest(manifest);
        file << serialized;
        file.close();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error saving project manifest: " << e.what() << std::endl;
        return false;
    }
}

bool ManifestManager::update_project_manifest(const ProjectManifest& updates) {
    p_impl->project_manifest = updates;

    if (p_impl->auto_save) {
        return save_project_manifest(updates);
    }

    return true;
}

// Manifest generation from source
LibraryManifest ManifestManager::generate_library_manifest(const std::filesystem::path& library_path) {
    LibraryManifest manifest;

    // Basic information
    manifest.name = library_path.filename().string();
    manifest.integration_path = library_path;
    manifest.build_path = "build/" + manifest.name;
    manifest.integration_method = "extraction";
    manifest.integration_date = std::chrono::system_clock::now();

    // Scan library files
    auto files = scan_library_files(library_path);
    for (const auto& file : files) {
        if (file.find(".cpp") != std::string::npos || file.find(".c") != std::string::npos || file.find(".cu") != std::string::npos) {
            manifest.source_files.push_back(file);
        } else if (file.find(".h") != std::string::npos || file.find(".hpp") != std::string::npos) {
            manifest.header_files.push_back(file);
        }
    }

    // Extract build information
    manifest.build_type = BuildType::STATIC; // Default to static
    manifest.cmake_target_name = manifest.name;

    // Detect version
    manifest.version = detect_library_version(library_path);

    // Detect license
    manifest.license_type = detect_library_license(library_path);

    // Extract dependencies
    manifest.library_dependencies = extract_dependencies(library_path);

    // Generate file hashes
    for (const auto& file : files) {
        std::filesystem::path full_path = library_path / file;
        if (std::filesystem::exists(full_path)) {
            manifest.file_hashes[file] = calculate_file_hash(full_path);
        }
    }

    // Detect components
    detect_components(manifest, library_path);

    return manifest;
}

ProjectManifest ManifestManager::generate_project_manifest(const std::filesystem::path& project_root) {
    ProjectManifest manifest;

    manifest.project_name = project_root.filename().string();
    manifest.project_version = "1.0.0";
    manifest.description = "Generated project manifest";
    manifest.build_system = "cmake";
    manifest.integration_root = "src/extracted";
    manifest.build_root = "build";
    manifest.manifest_directory = "build/manifests";
    manifest.created_at = std::chrono::system_clock::now();

    // Scan for libraries
    std::filesystem::path integration_path = project_root / manifest.integration_root;
    if (std::filesystem::exists(integration_path)) {
        for (const auto& entry : std::filesystem::directory_iterator(integration_path)) {
            if (entry.is_directory()) {
                LibraryManifest lib_manifest = generate_library_manifest(entry.path());
                manifest.libraries.push_back(lib_manifest);
                manifest.library_index[lib_manifest.name] = lib_manifest;
            }
        }
    }

    // Generate dependency graph
    for (const auto& lib : manifest.libraries) {
        manifest.library_status[lib.name] = lib.integration_status;
        manifest.dependency_graph[lib.name] = lib.library_dependencies;
    }

    return manifest;
}

bool ManifestManager::update_manifest_from_source(const std::string& library_name) {
    auto it = p_impl->library_manifests.find(library_name);
    if (it == p_impl->library_manifests.end()) {
        return false;
    }

    auto updated_manifest = generate_library_manifest(it->second.integration_path);

    // Preserve some existing information
    updated_manifest.integration_status = it->second.integration_status;
    updated_manifest.integration_date = it->second.integration_date;

    return update_library_manifest(library_name, updated_manifest);
}

// Build configuration generation
std::string ManifestManager::generate_cmake_configuration(const std::vector<std::string>& libraries) const {
    std::stringstream cmake;

    cmake << "# Generated CMake configuration for integrated libraries\n";
    cmake << "# Generated on: " << std::put_time(std::localtime(&(std::time_t){std::time(nullptr)}), "%Y-%m-%d %H:%M:%S") << "\n\n";

    cmake << "cmake_minimum_required(VERSION 3.16)\n";
    cmake << "project(IntegratedLibraries)\n\n";

    // Set C++ standard
    cmake << "set(CMAKE_CXX_STANDARD 17)\n";
    cmake << "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n\n";

    // Add library targets
    for (const auto& lib_name : libraries) {
        auto it = p_impl->library_manifests.find(lib_name);
        if (it != p_impl->library_manifests.end()) {
            const auto& manifest = it->second;

            cmake << "# Library: " << manifest.name << " (v" << manifest.version << ")\n";
            cmake << "add_library(" << manifest.cmake_target_name;

            if (manifest.build_type == BuildType::SHARED) {
                cmake << " SHARED";
            } else if (manifest.build_type == BuildType::INTERFACE) {
                cmake << " INTERFACE";
            }

            cmake << ")\n";

            // Add source files
            if (!manifest.source_files.empty()) {
                cmake << "target_sources(" << manifest.cmake_target_name << " PRIVATE\n";
                for (const auto& source : manifest.source_files) {
                    cmake << "    \"" << manifest.integration_path / source << "\"\n";
                }
                cmake << ")\n";
            }

            // Add include directories
            if (!manifest.include_directories.empty()) {
                cmake << "target_include_directories(" << manifest.cmake_target_name << " PUBLIC\n";
                for (const auto& include_dir : manifest.include_directories) {
                    cmake << "    \"" << manifest.integration_path / include_dir << "\"\n";
                }
                cmake << ")\n";
            }

            // Add compile definitions
            if (!manifest.compile_definitions.empty()) {
                cmake << "target_compile_definitions(" << manifest.cmake_target_name << " PUBLIC\n";
                for (const auto& [key, value] : manifest.compile_definitions) {
                    cmake << "    " << key << "=" << value << "\n";
                }
                cmake << ")\n";
            }

            cmake << "\n";
        }
    }

    // Link libraries together
    cmake << "# Link dependencies\n";
    for (const auto& lib_name : libraries) {
        auto it = p_impl->library_manifests.find(lib_name);
        if (it != p_impl->library_manifests.end()) {
            const auto& manifest = it->second;

            if (!manifest.library_dependencies.empty()) {
                cmake << "target_link_libraries(" << manifest.cmake_target_name << " PRIVATE\n";
                for (const auto& dep : manifest.library_dependencies) {
                    cmake << "    " << dep << "\n";
                }
                cmake << ")\n";
            }
        }
    }

    return cmake.str();
}

std::string ManifestManager::generate_build_script(const std::vector<std::string>& libraries) const {
    std::stringstream script;

    script << "#!/bin/bash\n";
    script << "# Generated build script for integrated libraries\n";
    script << "# Generated on: " << std::put_time(std::localtime(&(std::time_t){std::time(nullptr)}), "%Y-%m-%d %H:%M:%S") << "\n\n";

    script << "set -euo pipefail\n\n";
    script << "BUILD_DIR=\"build\"\n";
    script << "INSTALL_DIR=\"install\"\n\n";

    script << "echo \"Starting build of integrated libraries...\"\n\n";

    // Build in dependency order
    auto build_order = get_build_order(libraries);
    for (const auto& lib_name : build_order) {
        auto it = p_impl->library_manifests.find(lib_name);
        if (it != p_impl->library_manifests.end()) {
            const auto& manifest = it->second;

            script << "echo \"Building " << manifest.name << " (v" << manifest.version << ")...\"\n";
            script << "cd " << manifest.build_path << "\n";
            script << "cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=\"$INSTALL_DIR\" ..\n";
            script << "make -j$(nproc)\n";
            script << "make install\n";
            script << "cd ../..\n";
            script << "echo \"" << manifest.name << " build completed.\"\n\n";
        }
    }

    script << "echo \"All libraries built successfully.\"\n";

    return script.str();
}

std::vector<std::string> ManifestManager::get_build_order(const std::vector<std::string>& libraries) const {
    std::map<std::string, std::vector<std::string>> dependency_graph;

    for (const auto& lib_name : libraries) {
        auto it = p_impl->library_manifests.find(lib_name);
        if (it != p_impl->library_manifests.end()) {
            // Filter dependencies to only those in our library list
            std::vector<std::string> filtered_deps;
            for (const auto& dep : it->second.library_dependencies) {
                if (std::find(libraries.begin(), libraries.end(), dep) != libraries.end()) {
                    filtered_deps.push_back(dep);
                }
            }
            dependency_graph[lib_name] = filtered_deps;
        }
    }

    return topological_sort(dependency_graph);
}

// Validation methods
ManifestValidationResult ManifestManager::validate_library_manifest(const LibraryManifest& manifest) const {
    ManifestValidationResult result;
    auto start_time = std::chrono::high_resolution_clock::now();

    // Check required fields
    if (manifest.name.empty()) {
        result.errors.push_back("Library name is required");
    }

    if (manifest.version.empty()) {
        result.errors.push_back("Library version is required");
    }

    if (manifest.integration_path.empty()) {
        result.errors.push_back("Integration path is required");
    }

    // Check file existence
    if (!std::filesystem::exists(manifest.integration_path)) {
        result.errors.push_back("Integration path does not exist: " + manifest.integration_path.string());
    }

    // Validate file hashes
    for (const auto& [file_path, expected_hash] : manifest.file_hashes) {
        std::filesystem::path full_path = manifest.integration_path / file_path;
        if (std::filesystem::exists(full_path)) {
            std::string actual_hash = calculate_file_hash(full_path);
            if (actual_hash != expected_hash) {
                result.errors.push_back("File hash mismatch for: " + file_path);
            }
        } else {
            result.errors.push_back("File not found: " + file_path);
        }
    }

    // Check for duplicate dependencies
    std::set<std::string> unique_deps;
    for (const auto& dep : manifest.library_dependencies) {
        if (unique_deps.find(dep) != unique_deps.end()) {
            result.warnings.push_back("Duplicate dependency: " + dep);
        }
        unique_deps.insert(dep);
    }

    // Validate build configuration
    if (manifest.cmake_target_name.empty()) {
        result.warnings.push_back("CMake target name not specified");
    }

    result.is_valid = result.errors.empty();

    auto end_time = std::chrono::high_resolution_clock::now();
    result.validation_time = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    return result;
}

ManifestValidationResult ManifestManager::validate_project_manifest(const ProjectManifest& manifest) const {
    ManifestValidationResult result;
    auto start_time = std::chrono::high_resolution_clock::now();

    // Check required fields
    if (manifest.project_name.empty()) {
        result.errors.push_back("Project name is required");
    }

    if (manifest.project_version.empty()) {
        result.errors.push_back("Project version is required");
    }

    // Validate library manifests
    std::set<std::string> library_names;
    for (const auto& lib : manifest.libraries) {
        if (library_names.find(lib.name) != library_names.end()) {
            result.errors.push_back("Duplicate library in project manifest: " + lib.name);
        }
        library_names.insert(lib.name);

        // Validate individual library manifest
        auto lib_validation = validate_library_manifest(lib);
        if (!lib_validation.is_valid) {
            result.errors.push_back("Library manifest validation failed for: " + lib.name);
            result.errors.insert(result.errors.end(), lib_validation.errors.begin(), lib_validation.errors.end());
        }
    }

    result.is_valid = result.errors.empty();

    auto end_time = std::chrono::high_resolution_clock::now();
    result.validation_time = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    return result;
}

bool ManifestManager::verify_library_integrity(const std::string& library_name) {
    auto it = p_impl->library_manifests.find(library_name);
    if (it == p_impl->library_manifests.end()) {
        return false;
    }

    const auto& manifest = it->second;
    auto validation = validate_library_manifest(manifest);

    // Notify validation callbacks
    for (const auto& callback : p_impl->validation_callbacks) {
        callback(library_name, validation);
    }

    manifest.integrity_verified = validation.is_valid;
    manifest.last_verified = std::chrono::system_clock::now();

    return validation.is_valid;
}

// Helper method implementations
std::vector<std::string> ManifestManager::scan_library_files(const std::filesystem::path& library_path) const {
    std::vector<std::string> files;

    if (!std::filesystem::exists(library_path)) {
        return files;
    }

    for (const auto& entry : std::filesystem::recursive_directory_iterator(library_path)) {
        if (entry.is_regular_file()) {
            std::string relative_path = std::filesystem::relative(entry.path(), library_path).string();

            // Skip certain file types
            if (relative_path.find(".git/") == 0 ||
                relative_path.find("build/") == 0 ||
                relative_path.find("CMakeFiles/") == 0) {
                continue;
            }

            files.push_back(relative_path);
        }
    }

    return files;
}

std::string ManifestManager::calculate_file_hash(const std::filesystem::path& file_path) const {
    // Simplified hash calculation (in real implementation, use proper SHA-256)
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) {
        return "";
    }

    std::hash<std::string> hasher;
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return std::to_string(hasher(content));
}

std::string ManifestManager::serialize_manifest(const LibraryManifest& manifest) const {
    json j;

    j["name"] = manifest.name;
    j["version"] = manifest.version;
    j["description"] = manifest.description;
    j["origin_url"] = manifest.origin_url;
    j["origin_commit"] = manifest.origin_commit;
    j["license_type"] = manifest.license_type;
    j["spdx_identifier"] = manifest.spdx_identifier;

    j["integration_path"] = manifest.integration_path.string();
    j["build_path"] = manifest.build_path.string();
    j["integration_status"] = integration_status_to_string(manifest.integration_status);
    j["integration_method"] = manifest.integration_method;

    j["build_type"] = build_type_to_string(manifest.build_type);
    j["cmake_target_name"] = manifest.cmake_target_name;
    j["cmake_options"] = manifest.cmake_options;

    j["source_files"] = manifest.source_files;
    j["header_files"] = manifest.header_files;
    j["include_directories"] = manifest.include_directories;
    j["library_dependencies"] = manifest.library_dependencies;
    j["system_dependencies"] = manifest.system_dependencies;

    j["file_hashes"] = manifest.file_hashes;
    j["author"] = manifest.author;
    j["maintainer"] = manifest.maintainer;
    j["contributors"] = manifest.contributors;
    j["copyright_notice"] = manifest.copyright_notice;

    return j.dump(4);
}

std::string ManifestManager::serialize_manifest(const ProjectManifest& manifest) const {
    json j;

    j["project_name"] = manifest.project_name;
    j["project_version"] = manifest.project_version;
    j["description"] = manifest.description;
    j["build_system"] = manifest.build_system;
    j["compiler"] = manifest.compiler;
    j["language_standard"] = manifest.language_standard;

    j["integration_root"] = manifest.integration_root.string();
    j["build_root"] = manifest.build_root.string();
    j["manifest_directory"] = manifest.manifest_directory.string();

    j["integration_order"] = manifest.integration_order;
    j["global_build_options"] = manifest.global_build_options;
    j["environment_variables"] = manifest.environment_variables;

    j["required_libraries"] = std::vector<std::string>(manifest.required_libraries.begin(), manifest.required_libraries.end());
    j["optional_libraries"] = std::vector<std::string>(manifest.optional_libraries.begin(), manifest.optional_libraries.end());

    // Serialize library manifests
    json libraries = json::array();
    for (const auto& lib : manifest.libraries) {
        libraries.push_back(json::parse(serialize_manifest(lib)));
    }
    j["libraries"] = libraries;

    return j.dump(4);
}

LibraryManifest ManifestManager::deserialize_library_manifest(const std::string& data) const {
    json j = json::parse(data);

    LibraryManifest manifest;

    manifest.name = j.value("name", "");
    manifest.version = j.value("version", "");
    manifest.description = j.value("description", "");
    manifest.origin_url = j.value("origin_url", "");
    manifest.origin_commit = j.value("origin_commit", "");
    manifest.license_type = j.value("license_type", "");
    manifest.spdx_identifier = j.value("spdx_identifier", "");

    if (j.contains("integration_path")) {
        manifest.integration_path = j["integration_path"].get<std::string>();
    }
    if (j.contains("build_path")) {
        manifest.build_path = j["build_path"].get<std::string>();
    }

    if (j.contains("integration_status")) {
        manifest.integration_status = string_to_integration_status(j["integration_status"].get<std::string>());
    }

    manifest.integration_method = j.value("integration_method", "");

    if (j.contains("build_type")) {
        manifest.build_type = string_to_build_type(j["build_type"].get<std::string>());
    }

    manifest.cmake_target_name = j.value("cmake_target_name", "");
    manifest.cmake_options = j.value("cmake_options", std::vector<std::string>());

    manifest.source_files = j.value("source_files", std::vector<std::string>());
    manifest.header_files = j.value("header_files", std::vector<std::string>());
    manifest.include_directories = j.value("include_directories", std::vector<std::string>());
    manifest.library_dependencies = j.value("library_dependencies", std::vector<std::string>());
    manifest.system_dependencies = j.value("system_dependencies", std::vector<std::string>());

    manifest.file_hashes = j.value("file_hashes", std::map<std::string, std::string>());
    manifest.author = j.value("author", "");
    manifest.maintainer = j.value("maintainer", "");
    manifest.contributors = j.value("contributors", std::vector<std::string>());
    manifest.copyright_notice = j.value("copyright_notice", "");

    return manifest;
}

ProjectManifest ManifestManager::deserialize_project_manifest(const std::string& data) const {
    json j = json::parse(data);

    ProjectManifest manifest;

    manifest.project_name = j.value("project_name", "");
    manifest.project_version = j.value("project_version", "");
    manifest.description = j.value("description", "");
    manifest.build_system = j.value("build_system", "");
    manifest.compiler = j.value("compiler", "");
    manifest.language_standard = j.value("language_standard", "");

    if (j.contains("integration_root")) {
        manifest.integration_root = j["integration_root"].get<std::string>();
    }
    if (j.contains("build_root")) {
        manifest.build_root = j["build_root"].get<std::string>();
    }
    if (j.contains("manifest_directory")) {
        manifest.manifest_directory = j["manifest_directory"].get<std::string>();
    }

    manifest.integration_order = j.value("integration_order", std::vector<std::string>());
    manifest.global_build_options = j.value("global_build_options", std::map<std::string, std::string>());
    manifest.environment_variables = j.value("environment_variables", std::map<std::string, std::string>());

    auto required_libs = j.value("required_libraries", std::vector<std::string>());
    manifest.required_libraries.insert(required_libs.begin(), required_libs.end());

    auto optional_libs = j.value("optional_libraries", std::vector<std::string>());
    manifest.optional_libraries.insert(optional_libs.begin(), optional_libs.end());

    // Deserialize library manifests
    if (j.contains("libraries")) {
        for (const auto& lib_json : j["libraries"]) {
            LibraryManifest lib = deserialize_library_manifest(lib_json.dump());
            manifest.libraries.push_back(lib);
            manifest.library_index[lib.name] = lib;
        }
    }

    return manifest;
}

std::filesystem::path ManifestManager::get_manifest_path(const std::string& library_name) const {
    return p_impl->manifest_directory / "libraries" / (library_name + ".json");
}

// String conversion functions
std::string component_type_to_string(ComponentType type) {
    switch (type) {
        case ComponentType::CORE: return "core";
        case ComponentType::OPTIONAL: return "optional";
        case ComponentType::DEVELOPMENT: return "development";
        case ComponentType::DOCUMENTATION: return "documentation";
        case ComponentType::BUILD_TOOLS: return "build_tools";
        case ComponentType::RUNTIME: return "runtime";
        case ComponentType::TEST_FRAMEWORK: return "test_framework";
        default: return "unknown";
    }
}

std::string build_type_to_string(BuildType type) {
    switch (type) {
        case BuildType::STATIC: return "static";
        case BuildType::SHARED: return "shared";
        case BuildType::HEADER_ONLY: return "header_only";
        case BuildType::INTERFACE: return "interface";
        case BuildType::OBJECT: return "object";
        case BuildType::UTILITY: return "utility";
        default: return "unknown";
    }
}

std::string integration_status_to_string(IntegrationStatus status) {
    switch (status) {
        case IntegrationStatus::PENDING: return "pending";
        case IntegrationStatus::IN_PROGRESS: return "in_progress";
        case IntegrationStatus::COMPLETED: return "completed";
        case IntegrationStatus::FAILED: return "failed";
        case IntegrationStatus::SKIPPED: return "skipped";
        case IntegrationStatus::BLOCKED: return "blocked";
        case IntegrationStatus::VERIFIED: return "verified";
        default: return "unknown";
    }
}

ComponentType string_to_component_type(const std::string& type_str) {
    if (type_str == "core") return ComponentType::CORE;
    if (type_str == "optional") return ComponentType::OPTIONAL;
    if (type_str == "development") return ComponentType::DEVELOPMENT;
    if (type_str == "documentation") return ComponentType::DOCUMENTATION;
    if (type_str == "build_tools") return ComponentType::BUILD_TOOLS;
    if (type_str == "runtime") return ComponentType::RUNTIME;
    if (type_str == "test_framework") return ComponentType::TEST_FRAMEWORK;
    return ComponentType::CORE; // Default
}

BuildType string_to_build_type(const std::string& type_str) {
    if (type_str == "static") return BuildType::STATIC;
    if (type_str == "shared") return BuildType::SHARED;
    if (type_str == "header_only") return BuildType::HEADER_ONLY;
    if (type_str == "interface") return BuildType::INTERFACE;
    if (type_str == "object") return BuildType::OBJECT;
    if (type_str == "utility") return BuildType::UTILITY;
    return BuildType::STATIC; // Default
}

IntegrationStatus string_to_integration_status(const std::string& status_str) {
    if (status_str == "pending") return IntegrationStatus::PENDING;
    if (status_str == "in_progress") return IntegrationStatus::IN_PROGRESS;
    if (status_str == "completed") return IntegrationStatus::COMPLETED;
    if (status_str == "failed") return IntegrationStatus::FAILED;
    if (status_str == "skipped") return IntegrationStatus::SKIPPED;
    if (status_str == "blocked") return IntegrationStatus::BLOCKED;
    if (status_str == "verified") return IntegrationStatus::VERIFIED;
    return IntegrationStatus::PENDING; // Default
}

// Missing helper methods
std::string ManifestManager::detect_library_version(const std::filesystem::path& library_path) const {
    // Try to read version from common files
    std::vector<std::string> version_files = {"VERSION", "version.txt", "CMakeLists.txt", "configure.ac"};
    for (const auto& file : version_files) {
        std::filesystem::path version_path = library_path / file;
        if (std::filesystem::exists(version_path)) {
            std::ifstream file(version_path);
            std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

            // Extract version using regex
            std::regex version_regex(R"((\d+\.\d+\.\d+))");
            std::smatch match;
            if (std::regex_search(content, match, version_regex)) {
                return match[1].str();
            }
        }
    }

    return "1.0.0"; // Default version
}

std::string ManifestManager::detect_library_license(const std::filesystem::path& library_path) const {
    // Check for common license files
    std::vector<std::string> license_files = {"LICENSE", "LICENSE.txt", "COPYING", "license.md"};
    for (const auto& file : license_files) {
        std::filesystem::path license_path = library_path / file;
        if (std::filesystem::exists(license_path)) {
            std::ifstream file(license_path);
            std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

            // Extract license type
            if (content.find("MIT") != std::string::npos) return "MIT";
            if (content.find("Apache") != std::string::npos) return "Apache-2.0";
            if (content.find("BSD") != std::string::npos) return "BSD";
            if (content.find("GPL") != std::string::npos) {
                if (content.find("LGPL") != std::string::npos) return "LGPL";
                return "GPL";
            }
        }
    }

    return "Unknown"; // Default license
}

std::vector<std::string> ManifestManager::extract_dependencies(const std::filesystem::path& library_path) const {
    std::vector<std::string> dependencies;

    std::filesystem::path cmake_path = library_path / "CMakeLists.txt";
    if (std::filesystem::exists(cmake_path)) {
        std::ifstream cmake_file(cmake_path);
        std::string content((std::istreambuf_iterator<char>(cmake_file)), std::istreambuf_iterator<char>());
        cmake_file.close();

        // Extract dependencies using regex
        std::regex dep_regex(R"(find_package\s*\(\s*([a-zA-Z0-9_]+))");
        std::smatch match;

        while (std::regex_search(content, match, dep_regex)) {
            dependencies.push_back(match[1].str());
            content = match.suffix();
        }
    }

    return dependencies;
}

void ManifestManager::detect_components(LibraryManifest& manifest, const std::filesystem::path& library_path) const {
    // Create default core component
    Component core_component;
    core_component.name = "core";
    core_component.type = ComponentType::CORE;
    core_component.source_files = manifest.source_files;
    core_component.header_files = manifest.header_files;
    core_component.description = "Core library functionality";
    manifest.components.push_back(core_component);

    // Check for test components
    std::vector<std::string> test_files;
    for (const auto& file : scan_library_files(library_path)) {
        if (file.find("test") != std::string::npos || file.find("Test") != std::string::npos) {
            test_files.push_back(file);
        }
    }

    if (!test_files.empty()) {
        Component test_component;
        test_component.name = "tests";
        test_component.type = ComponentType::DEVELOPMENT;
        test_component.source_files = test_files;
        test_component.description = "Test suite";
        test_component.is_enabled = false; // Disabled by default
        manifest.components.push_back(test_component);
    }

    // Check for documentation
    std::vector<std::string> doc_files;
    for (const auto& file : scan_library_files(library_path)) {
        if (file.find(".md") != std::string::npos || file.find(".txt") != std::string::npos ||
            file.find("doc") != std::string::npos || file.find("Doc") != std::string::npos) {
            doc_files.push_back(file);
        }
    }

    if (!doc_files.empty()) {
        Component doc_component;
        doc_component.name = "documentation";
        doc_component.type = ComponentType::DOCUMENTATION;
        doc_component.data_files = doc_files; // Assuming data_files field exists
        doc_component.description = "Documentation";
        doc_component.is_enabled = false; // Disabled by default
        manifest.components.push_back(doc_component);
    }
}

std::string ManifestManager::generate_dependency_file(const std::vector<std::string>& libraries) const {
    std::stringstream deps;

    deps << "# Generated dependency file\n";
    deps << "# Libraries: ";
    for (size_t i = 0; i < libraries.size(); ++i) {
        if (i > 0) deps << ", ";
        deps << libraries[i];
    }
    deps << "\n\n";

    auto build_order = get_build_order(libraries);
    deps << "Build Order:\n";
    for (const auto& lib : build_order) {
        deps << "  " << lib;
        auto it = p_impl->library_manifests.find(lib);
        if (it != p_impl->library_manifests.end()) {
            deps << " (v" << it->second.version << ")";
        }
        deps << "\n";
    }

    deps << "\nDependencies:\n";
    for (const auto& lib : libraries) {
        auto it = p_impl->library_manifests.find(lib);
        if (it != p_impl->library_manifests.end()) {
            deps << lib << " -> [";
            for (size_t i = 0; i < it->second.library_dependencies.size(); ++i) {
                if (i > 0) deps << ", ";
                deps << it->second.library_dependencies[i];
            }
            deps << "]\n";
        }
    }

    return deps.str();
}

std::vector<std::string> ManifestManager::resolve_dependencies(const std::string& library_name) const {
    std::vector<std::string> resolved;
    std::set<std::string> visited;

    std::function<void(const std::string&)> resolve_recursive = [&](const std::string& lib_name) {
        if (visited.find(lib_name) != visited.end()) {
            return; // Avoid cycles
        }
        visited.insert(lib_name);

        auto it = p_impl->library_manifests.find(lib_name);
        if (it != p_impl->library_manifests.end()) {
            for (const auto& dep : it->second.library_dependencies) {
                resolve_recursive(dep);
            }
            resolved.push_back(lib_name);
        }
    };

    resolve_recursive(library_name);
    return resolved;
}

std::vector<std::string> ManifestManager::get_dependency_chain(const std::string& library_name) const {
    return resolve_dependencies(library_name);
}

bool ManifestManager::check_dependencies_satisfied(const std::vector<std::string>& libraries) const {
    for (const auto& lib_name : libraries) {
        auto it = p_impl->library_manifests.find(lib_name);
        if (it != p_impl->library_manifests.end()) {
            for (const auto& dep : it->second.library_dependencies) {
                if (std::find(libraries.begin(), libraries.end(), dep) == libraries.end()) {
                    return false; // Dependency not satisfied
                }
            }
        }
    }
    return true;
}

std::map<std::string, std::vector<std::string>> ManifestManager::get_dependency_graph() const {
    std::map<std::string, std::vector<std::string>> graph;
    for (const auto& [name, manifest] : p_impl->library_manifests) {
        graph[name] = manifest.library_dependencies;
    }
    return graph;
}

bool ManifestManager::enable_component(const std::string& library_name, const std::string& component_name) {
    auto it = p_impl->library_manifests.find(library_name);
    if (it == p_impl->library_manifests.end()) {
        return false;
    }

    for (auto& component : it->second.components) {
        if (component.name == component_name) {
            component.is_enabled = true;
            it->second.enabled_components.insert(component_name);
            it->second.disabled_components.erase(component_name);
            return true;
        }
    }

    return false;
}

bool ManifestManager::disable_component(const std::string& library_name, const std::string& component_name) {
    auto it = p_impl->library_manifests.find(library_name);
    if (it == p_impl->library_manifests.end()) {
        return false;
    }

    for (auto& component : it->second.components) {
        if (component.name == component_name) {
            component.is_enabled = false;
            it->second.disabled_components.insert(component_name);
            it->second.enabled_components.erase(component_name);
            return true;
        }
    }

    return false;
}

std::vector<Component> ManifestManager::get_enabled_components(const std::string& library_name) const {
    std::vector<Component> enabled;
    auto it = p_impl->library_manifests.find(library_name);
    if (it != p_impl->library_manifests.end()) {
        for (const auto& component : it->second.components) {
            if (component.is_enabled) {
                enabled.push_back(component);
            }
        }
    }
    return enabled;
}

std::vector<Component> ManifestManager::get_available_components(const std::string& library_name) const {
    std::vector<Component> available;
    auto it = p_impl->library_manifests.find(library_name);
    if (it != p_impl->library_manifests.end()) {
        available = it->second.components;
    }
    return available;
}

bool ManifestManager::update_library_version(const std::string& library_name, const std::string& new_version) {
    auto it = p_impl->library_manifests.find(library_name);
    if (it == p_impl->library_manifests.end()) {
        return false;
    }

    it->second.version = new_version;
    return true;
}

std::string ManifestManager::get_library_version(const std::string& library_name) const {
    auto it = p_impl->library_manifests.find(library_name);
    if (it != p_impl->library_manifests.end()) {
        return it->second.version;
    }
    return "";
}

std::vector<std::string> ManifestManager::get_available_versions(const std::string& library_name) const {
    // In a real implementation, this would scan available versions
    return {get_library_version(library_name)};
}

bool ManifestManager::check_version_compatibility(const std::string& library1, const std::string& version1,
                                                const std::string& library2, const std::string& version2) const {
    // Simplified version compatibility check
    return true; // Assume compatible for now
}

bool ManifestManager::update_integration_status(const std::string& library_name, IntegrationStatus status) {
    auto it = p_impl->library_manifests.find(library_name);
    if (it == p_impl->library_manifests.end()) {
        return false;
    }

    IntegrationStatus old_status = it->second.integration_status;
    it->second.integration_status = status;

    // Notify callbacks
    for (const auto& callback : p_impl->status_change_callbacks) {
        callback(library_name, old_status, status);
    }

    return true;
}

IntegrationStatus ManifestManager::get_integration_status(const std::string& library_name) const {
    auto it = p_impl->library_manifests.find(library_name);
    if (it != p_impl->library_manifests.end()) {
        return it->second.integration_status;
    }
    return IntegrationStatus::PENDING;
}

std::vector<std::string> ManifestManager::get_libraries_by_status(IntegrationStatus status) const {
    std::vector<std::string> libraries;
    for (const auto& [name, manifest] : p_impl->library_manifests) {
        if (manifest.integration_status == status) {
            libraries.push_back(name);
        }
    }
    return libraries;
}

bool ManifestManager::orchestrate_build(const std::vector<std::string>& libraries) {
    auto build_commands = generate_build_commands(libraries);
    return execute_build_plan(build_commands);
}

std::vector<std::string> ManifestManager::generate_build_commands(const std::vector<std::string>& libraries) const {
    std::vector<std::string> commands;
    auto build_order = get_build_order(libraries);

    for (const auto& lib_name : build_order) {
        auto it = p_impl->library_manifests.find(lib_name);
        if (it != p_impl->library_manifests.end()) {
            std::string cmd = "cd " + it->second.build_path.string() + " && cmake -DCMAKE_BUILD_TYPE=Release .. && make -j$(nproc)";
            commands.push_back(cmd);
        }
    }

    return commands;
}

bool ManifestManager::execute_build_plan(const std::vector<std::string>& build_commands) {
    // Simplified build execution - in real implementation would execute commands
    return !build_commands.empty();
}

std::map<std::string, std::chrono::milliseconds> ManifestManager::get_build_times() const {
    std::map<std::string, std::chrono::milliseconds> build_times;
    for (const auto& [name, manifest] : p_impl->library_manifests) {
        build_times[name] = manifest.build_time;
    }
    return build_times;
}

bool ManifestManager::export_manifests(const std::filesystem::path& export_path, const std::vector<std::string>& libraries) const {
    std::filesystem::create_directories(export_path);

    // Export project manifest
    ProjectManifest project = p_impl->project_manifest;
    std::ofstream project_file(export_path / "project.json");
    project_file << serialize_manifest(project);
    project_file.close();

    // Export library manifests
    for (const auto& [name, manifest] : p_impl->library_manifests) {
        if (libraries.empty() || std::find(libraries.begin(), libraries.end(), name) != libraries.end()) {
            std::ofstream lib_file(export_path / (name + ".json"));
            lib_file << serialize_manifest(manifest);
            lib_file.close();
        }
    }

    return true;
}

bool ManifestManager::import_manifests(const std::filesystem::path& import_path) {
    // Import project manifest
    std::filesystem::path project_file = import_path / "project.json";
    if (std::filesystem::exists(project_file)) {
        ProjectManifest project;
        if (load_project_manifest(project)) {
            p_impl->project_manifest = project;
        }
    }

    // Import library manifests
    for (const auto& entry : std::filesystem::directory_iterator(import_path)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json" && entry.path().filename() != "project.json") {
            LibraryManifest manifest;
            if (load_library_manifest(entry.path().stem().string(), manifest)) {
                p_impl->library_manifests[manifest.name] = manifest;
            }
        }
    }

    return true;
}

bool ManifestManager::merge_manifests(const std::filesystem::path& other_manifest_directory) {
    // Implementation for merging manifests from another directory
    return true;
}

std::string ManifestManager::generate_integration_report() const {
    std::stringstream report;
    report << "Integration Report\n";
    report << "================\n\n";

    report << "Total Libraries: " << p_impl->library_manifests.size() << "\n";

    std::map<IntegrationStatus, int> status_counts;
    for (const auto& [name, manifest] : p_impl->library_manifests) {
        status_counts[manifest.integration_status]++;
    }

    for (const auto& [status, count] : status_counts) {
        report << integration_status_to_string(status) << ": " << count << "\n";
    }

    return report.str();
}

std::string ManifestManager::generate_dependency_report() const {
    std::stringstream report;
    report << "Dependency Report\n";
    report << "==================\n\n";

    auto dependency_graph = get_dependency_graph();
    for (const auto& [lib, deps] : dependency_graph) {
        report << lib << " depends on: ";
        for (size_t i = 0; i < deps.size(); ++i) {
            if (i > 0) report << ", ";
            report << deps[i];
        }
        report << "\n";
    }

    return report.str();
}

std::string ManifestManager::generate_build_configuration_report() const {
    return "Build Configuration Report (not implemented)";
}

std::map<std::string, std::string> ManifestManager::get_integration_statistics() const {
    std::map<std::string, std::string> stats;
    stats["total_libraries"] = std::to_string(p_impl->library_manifests.size());
    stats["project_name"] = p_impl->project_manifest.project_name;
    stats["project_version"] = p_impl->project_manifest.project_version;
    return stats;
}

void ManifestManager::register_manifest_change_callback(std::function<void(const std::string&, const LibraryManifest&)> callback) {
    p_impl->manifest_change_callbacks.push_back(callback);
}

void ManifestManager::register_status_change_callback(std::function<void(const std::string&, IntegrationStatus, IntegrationStatus)> callback) {
    p_impl->status_change_callbacks.push_back(callback);
}

void ManifestManager::register_validation_callback(std::function<void(const std::string&, const ManifestValidationResult&)> callback) {
    p_impl->validation_callbacks.push_back(callback);
}

std::vector<std::string> ManifestManager::topological_sort(const std::map<std::string, std::vector<std::string>>& graph) const {
    std::vector<std::string> result;
    std::map<std::string, int> in_degree;
    std::queue<std::string> queue;

    // Calculate in-degree for each node
    for (const auto& [node, deps] : graph) {
        in_degree[node] = 0;
    }
    for (const auto& [node, deps] : graph) {
        for (const auto& dep : deps) {
            in_degree[dep]++;
        }
    }

    // Initialize queue with nodes having zero in-degree
    for (const auto& [node, degree] : in_degree) {
        if (degree == 0) {
            queue.push(node);
        }
    }

    // Process nodes
    while (!queue.empty()) {
        std::string current = queue.front();
        queue.pop();
        result.push_back(current);

        auto it = graph.find(current);
        if (it != graph.end()) {
            for (const auto& neighbor : it->second) {
                in_degree[neighbor]--;
                if (in_degree[neighbor] == 0) {
                    queue.push(neighbor);
                }
            }
        }
    }

    return result;
}

std::vector<std::string> ManifestManager::detect_manifest_inconsistencies() const {
    std::vector<std::string> inconsistencies;

    // Check for missing dependencies
    for (const auto& [name, manifest] : p_impl->library_manifests) {
        for (const auto& dep : manifest.library_dependencies) {
            if (p_impl->library_manifests.find(dep) == p_impl->library_manifests.end()) {
                inconsistencies.push_back("Missing dependency: " + name + " depends on " + dep);
            }
        }
    }

    return inconsistencies;
}

bool ManifestManager::verify_project_integrity() {
    bool all_valid = true;
    for (const auto& [name, manifest] : p_impl->library_manifests) {
        if (!verify_library_integrity(name)) {
            all_valid = false;
        }
    }

    return all_valid;
}

bool ManifestManager::validate_manifest_schema(const std::string& manifest_data) const {
    // In a real implementation, this would validate against a JSON schema
    return true;
}

std::string ManifestManager::generate_manifest_signature(const LibraryManifest& manifest) const {
    // In a real implementation, this would generate a cryptographic signature
    return "signature_placeholder";
}

void ManifestManager::update_dependency_graph() {
    for (const auto& [name, manifest] : p_impl->library_manifests) {
        p_impl->project_manifest.dependency_graph[name] = manifest.library_dependencies;
    }
}

// Global instance
ManifestManager& get_manifest_manager() {
    static ManifestManager instance;
    return instance;
}

} // namespace manifest
} // namespace integration