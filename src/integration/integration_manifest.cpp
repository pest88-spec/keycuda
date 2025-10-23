/**
 * @file integration_manifest.cpp
 * @brief Integration manifest system implementation
 *
 * Implementation of the integration manifest system for build configuration
 * management. Provides comprehensive manifest handling, validation, and
 * CMake configuration generation.
 *
 * Created: 2025-10-22
 * Feature: Third-Party Dependencies Integration Optimization
 */

#include "integration_manifest.h"
#include <fstream>
#include <filesystem>
#include <sstream>
#include <algorithm>
#include <iomanip>
#include <random>
#include <regex>

namespace integration {
namespace manifest {

// Implementation structure
struct StandardManifestManager::Impl {
    std::string manifests_dir;
    std::vector<IntegrationManifest> manifests;
    IntegrationConfiguration integration_config;
    bool is_initialized = false;

    Impl() {
        // Set default integration configuration
        integration_config = {
            .enable_strict_attribution = true,
            .preserve_original_structure = true,
            .add_namespace_prefix = false,
            .namespace_prefix = "",
            .source_modifications = {},
            .compile_flags = {},
            .enable_offline_builds = true
        };
    }

    std::string get_manifest_path(const std::string& manifest_id) const {
        return manifests_dir + "/" + manifest_id + ".json";
    }

    std::optional<IntegrationManifest> load_manifest_from_file(const std::string& file_path) {
        std::ifstream file(file_path);
        if (!file.is_open()) {
            return std::nullopt;
        }

        std::string json_content((std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());

        return manifest_from_json(json_content);
    }

    bool save_manifest_to_file(const IntegrationManifest& manifest, const std::string& file_path) {
        std::ofstream file(file_path);
        if (!file.is_open()) {
            return false;
        }

        std::string json_content = manifest_to_json(manifest);
        file << json_content;
        return file.good();
    }

    void ensure_directory_exists() {
        std::filesystem::create_directories(manifests_dir);
    }

    std::vector<LibraryInfo> get_all_libraries() const {
        std::vector<LibraryInfo> all_libraries;
        for (const auto& manifest : manifests) {
            all_libraries.insert(all_libraries.end(),
                                manifest.libraries.begin(),
                                manifest.libraries.end());
        }
        return all_libraries;
    }

    std::optional<LibraryInfo*> find_library(const std::string& library_name) {
        for (auto& manifest : manifests) {
            for (auto& library : manifest.libraries) {
                if (library.library_name == library_name) {
                    return &library;
                }
            }
        }
        return std::nullopt;
    }

    std::optional<const LibraryInfo*> find_library(const std::string& library_name) const {
        for (const auto& manifest : manifests) {
            for (const auto& library : manifest.libraries) {
                if (library.library_name == library_name) {
                    return &library;
                }
            }
        }
        return std::nullopt;
    }
};

// StandardManifestManager implementation
StandardManifestManager::StandardManifestManager() : p_impl(std::make_unique<Impl>()) {}

StandardManifestManager::~StandardManifestManager() = default;

bool StandardManifestManager::initialize(const std::string& manifests_dir) {
    p_impl->manifests_dir = manifests_dir;
    p_impl->ensure_directory_exists();

    // Load existing manifests
    if (std::filesystem::exists(p_impl->manifests_dir)) {
        for (const auto& entry : std::filesystem::directory_iterator(p_impl->manifests_dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                auto manifest = p_impl->load_manifest_from_file(entry.path().string());
                if (manifest) {
                    p_impl->manifests.push_back(*manifest);
                }
            }
        }
    }

    p_impl->is_initialized = true;
    return true;
}

IntegrationManifest StandardManifestManager::create_library_manifest(
    const std::string& library_name,
    const std::string& library_version,
    const std::string& source_path) {

    IntegrationManifest manifest;
    manifest.manifest_id = generate_manifest_id(library_name);
    manifest.manifest_type = ManifestType::LIBRARY_MANIFEST;
    manifest.schema_version = "1.0";
    manifest.created_at = std::chrono::system_clock::now();
    manifest.created_by = "integration_manifest_manager";
    manifest.description = "Manifest for integrated library: " + library_name;
    manifest.manifest_version = "1.0";

    LibraryInfo library;
    library.library_name = library_name;
    library.library_version = library_version;
    library.origin_url = "Unknown";
    library.commit_hash = "Unknown";
    library.source_path = source_path;
    library.build_path = "build/" + library_name;
    library.cmake_targets = {library_name};
    library.is_enabled = true;
    library.last_updated = std::chrono::system_clock::now();

    // Default build options
    library.build_options = {
        {"BUILD_SHARED_LIBS", "OFF"},
        {"CMAKE_POSITION_INDEPENDENT_CODE", "ON"},
        {"BUILD_TESTING", "OFF"}
    };

    // Default include directories
    library.include_directories = {"include"};

    library.metadata = {
        {"auto_detected", "false"},
        {"integration_date", "2025-10-22"}
    };

    manifest.libraries.push_back(library);

    // Default build configuration
    manifest.build_config = {
        .build_type = "RelWithDebInfo",
        .toolchain_version = "GCC 11+ / CUDA 12.0+",
        .compiler_flags = {"-Wall", "-Wextra", "-O3"},
        .cmake_definitions = {
            "-DCMAKE_BUILD_TYPE=RelWithDebInfo",
            "-DCMAKE_CUDA_ARCHITECTURES=75;86;89;90"
        },
        .install_prefix = "/usr/local",
        .is_offline_build = false,
        .cuda_architecture = "75;86;89;90",
        .required_tools = {"cmake", "make", "gcc", "g++", "nvcc"}
    };

    manifest.tags = {"library", "managed"};

    return manifest;
}

std::optional<IntegrationManifest> StandardManifestManager::load_manifest(const std::string& manifest_id) {
    if (!p_impl->is_initialized) {
        return std::nullopt;
    }

    std::string file_path = p_impl->get_manifest_path(manifest_id);
    return p_impl->load_manifest_from_file(file_path);
}

bool StandardManifestManager::save_manifest(const IntegrationManifest& manifest) {
    if (!p_impl->is_initialized) {
        return false;
    }

    // Validate before saving
    ValidationResult validation = validate_manifest(manifest);
    if (!validation.is_valid) {
        return false;
    }

    std::string file_path = p_impl->get_manifest_path(manifest.manifest_id);

    if (p_impl->save_manifest_to_file(manifest, file_path)) {
        // Update in-memory cache
        auto it = std::find_if(p_impl->manifests.begin(), p_impl->manifests.end(),
            [&](const IntegrationManifest& m) { return m.manifest_id == manifest.manifest_id; });

        if (it != p_impl->manifests.end()) {
            *it = manifest;
        } else {
            p_impl->manifests.push_back(manifest);
        }

        return true;
    }

    return false;
}

std::vector<IntegrationManifest> StandardManifestManager::get_all_manifests(
    std::optional<ManifestType> type) {

    if (!p_impl->is_initialized) {
        return {};
    }

    if (!type) {
        return p_impl->manifests;
    }

    std::vector<IntegrationManifest> filtered;
    for (const auto& manifest : p_impl->manifests) {
        if (manifest.manifest_type == *type) {
            filtered.push_back(manifest);
        }
    }

    return filtered;
}

std::vector<IntegrationManifest> StandardManifestManager::find_manifests_by_library(
    const std::string& library_name) {

    if (!p_impl->is_initialized) {
        return {};
    }

    std::vector<IntegrationManifest> found;
    for (const auto& manifest : p_impl->manifests) {
        for (const auto& library : manifest.libraries) {
            if (library.library_name == library_name) {
                found.push_back(manifest);
                break;
            }
        }
    }

    return found;
}

bool StandardManifestManager::update_library(const std::string& manifest_id,
                                            const LibraryInfo& library_info) {
    if (!p_impl->is_initialized) {
        return false;
    }

    auto it = std::find_if(p_impl->manifests.begin(), p_impl->manifests.end(),
        [&](const IntegrationManifest& m) { return m.manifest_id == manifest_id; });

    if (it == p_impl->manifests.end()) {
        return false;
    }

    // Find and update the library
    auto lib_it = std::find_if(it->libraries.begin(), it->libraries.end(),
        [&](const LibraryInfo& lib) { return lib.library_name == library_info.library_name; });

    if (lib_it != it->libraries.end()) {
        *lib_it = library_info;
        return save_manifest(*it);
    }

    return false;
}

bool StandardManifestManager::remove_library(const std::string& manifest_id,
                                             const std::string& library_name) {
    if (!p_impl->is_initialized) {
        return false;
    }

    auto it = std::find_if(p_impl->manifests.begin(), p_impl->manifests.end(),
        [&](const IntegrationManifest& m) { return m.manifest_id == manifest_id; });

    if (it == p_impl->manifests.end()) {
        return false;
    }

    // Remove the library
    auto lib_it = std::remove_if(it->libraries.begin(), it->libraries.end(),
        [&](const LibraryInfo& lib) { return lib.library_name == library_name; });

    if (lib_it != it->libraries.end()) {
        it->libraries.erase(lib_it, it->libraries.end());
        return save_manifest(*it);
    }

    return false;
}

ValidationResult StandardManifestManager::validate_manifest(const IntegrationManifest& manifest) {
    ValidationResult result;
    result.is_valid = true;

    // Validate required fields
    if (manifest.manifest_id.empty()) {
        result.is_valid = false;
        result.errors.push_back("Manifest ID is required");
    }

    if (manifest.schema_version.empty()) {
        result.is_valid = false;
        result.errors.push_back("Schema version is required");
    }

    if (manifest.manifest_version.empty()) {
        result.is_valid = false;
        result.errors.push_back("Manifest version is required");
    }

    // Validate libraries
    for (const auto& library : manifest.libraries) {
        if (library.library_name.empty()) {
            result.is_valid = false;
            result.errors.push_back("Library name is required");
        }

        if (library.library_version.empty()) {
            result.warnings.push_back("Library '" + library.library_name + "' has no version specified");
        }

        if (library.source_path.empty()) {
            result.is_valid = false;
            result.errors.push_back("Source path is required for library '" + library.library_name + "'");
        } else if (!std::filesystem::exists(library.source_path)) {
            result.warnings.push_back("Source path does not exist for library '" + library.library_name + "': " + library.source_path);
        }

        if (library.cmake_targets.empty()) {
            result.warnings.push_back("No CMake targets specified for library '" + library.library_name + "'");
        }
    }

    // Validate build configuration
    if (manifest.build_config.build_type.empty()) {
        result.warnings.push_back("Build type not specified, using default");
    }

    if (manifest.build_config.required_tools.empty()) {
        result.warnings.push_back("No required tools specified in build configuration");
    }

    // Check for duplicate libraries
    std::set<std::string> library_names;
    for (const auto& library : manifest.libraries) {
        if (library_names.count(library.library_name)) {
            result.errors.push_back("Duplicate library name: " + library.library_name);
            result.is_valid = false;
        }
        library_names.insert(library.library_name);
    }

    return result;
}

std::string StandardManifestManager::generate_cmake_configuration() {
    if (!p_impl->is_initialized) {
        return "";
    }

    std::ostringstream cmake_content;
    cmake_content << "# Auto-generated CMake configuration from integration manifests\n";
    cmake_content << "# Generated on: " << std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count() << "\n\n";

    // Add build configuration
    cmake_content << "# Build Configuration\n";
    cmake_content << "set(CMAKE_BUILD_TYPE " << p_impl->manifests.empty() ?
        "RelWithDebInfo" : p_impl->manifests[0].build_config.build_type << " CACHE STRING \"Build type\")\n";

    // Add CUDA configuration if available
    bool has_cuda = false;
    for (const auto& manifest : p_impl->manifests) {
        if (!manifest.build_config.cuda_architecture.empty()) {
            has_cuda = true;
            break;
        }
    }

    if (has_cuda) {
        cmake_content << "set(CMAKE_CUDA_ARCHITECTURES " <<
            p_impl->manifests[0].build_config.cuda_architecture << " CACHE STRING \"CUDA architectures\")\n";
    }

    cmake_content << "\n# Extracted Libraries\n";

    // Add each library
    auto all_libraries = p_impl->get_all_libraries();
    for (const auto& library : all_libraries) {
        if (!library.is_enabled) {
            continue;
        }

        cmake_content << "\n# Library: " << library.library_name << "\n";

        // Add source files
        cmake_content << "set(" << library.library_name << "_SOURCES\n";
        if (std::filesystem::exists(library.source_path)) {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(library.source_path)) {
                if (entry.is_regular_file()) {
                    std::string ext = entry.path().extension().string();
                    if (ext == ".c" || ext == ".cpp" || ext == ".cxx") {
                        cmake_content << "    " << entry.path().string() << "\n";
                    }
                }
            }
        }
        cmake_content << ")\n";

        // Add include directories
        if (!library.include_directories.empty()) {
            cmake_content << "set(" << library.library_name << "_INCLUDE_DIRS\n";
            for (const auto& include_dir : library.include_directories) {
                std::string full_path = library.source_path + "/" + include_dir;
                cmake_content << "    " << full_path << "\n";
            }
            cmake_content << ")\n";
        }

        // Add build options
        cmake_content << "set(" << library.library_name << "_BUILD_OPTIONS\n";
        for (const auto& [option, value] : library.build_options) {
            cmake_content << "    -D" << option << "=" << value << "\n";
        }
        cmake_content << ")\n";

        // Create library target
        cmake_content << "add_library(" << library.library_name << " ";
        cmake_content << "${" << library.library_name << "_SOURCES})\n";

        // Add include directories
        if (!library.include_directories.empty()) {
            cmake_content << "target_include_directories(" << library.library_name << " ";
            cmake_content << "PRIVATE ${" << library.library_name << "_INCLUDE_DIRS})\n";
        }

        // Add link libraries
        if (!library.link_libraries.empty()) {
            cmake_content << "target_link_libraries(" << library.library_name << " ";
            for (const auto& lib : library.link_libraries) {
                cmake_content << lib << " ";
            }
            cmake_content << ")\n";
        }

        // Add compile definitions
        cmake_content << "target_compile_definitions(" << library.library_name << " ";
        cmake_content << "PRIVATE ${" << library.library_name << "_BUILD_OPTIONS})\n";
    }

    cmake_content << "\n# Integration Manifest Configuration Complete\n";

    return cmake_content.str();
}

DependencyManifest StandardManifestManager::get_dependency_manifest() {
    DependencyManifest dep_manifest;
    dep_manifest.manifest_version = "1.0";
    dep_manifest.libraries = p_impl->get_all_libraries();

    // Add build sources from all libraries
    for (const auto& library : dep_manifest.libraries) {
        if (std::filesystem::exists(library.source_path)) {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(library.source_path)) {
                if (entry.is_regular_file()) {
                    std::string ext = entry.path().extension().string();
                    if (ext == ".c" || ext == ".cpp" || ext == ".h" || ext == ".hpp") {
                        dep_manifest.build_sources.push_back(entry.path().string());
                    }
                }
            }
        }
    }

    // Default exclude patterns
    dep_manifest.exclude_patterns = {
        "*/test*",
        "*/tests/*",
        "*/Test*",
        "*_test.*",
        "*_test_.*",
        "*/examples/*",
        "*/docs/*",
        "*/documentation/*",
        "*.md",
        "*.txt",
        "*.rst"
    };

    return dep_manifest;
}

IntegrationConfiguration StandardManifestManager::get_integration_configuration() {
    return p_impl->integration_config;
}

bool StandardManifestManager::set_integration_configuration(const IntegrationConfiguration& config) {
    p_impl->integration_config = config;
    return true;
}

std::vector<std::string> StandardManifestManager::check_conflicts() {
    std::vector<std::string> conflicts;
    auto all_libraries = p_impl->get_all_libraries();

    // Check for version conflicts
    std::map<std::string, std::vector<std::string>> version_map;
    for (const auto& library : all_libraries) {
        version_map[library.library_name].push_back(library.library_version);
    }

    for (const auto& [name, versions] : version_map) {
        if (versions.size() > 1) {
            std::string conflict = "Version conflict for library '" + name + "': ";
            for (size_t i = 0; i < versions.size(); ++i) {
                if (i > 0) conflict += ", ";
                conflict += versions[i];
            }
            conflicts.push_back(conflict);
        }
    }

    // Check for path conflicts
    std::map<std::string, std::vector<std::string>> path_map;
    for (const auto& library : all_libraries) {
        path_map[library.source_path].push_back(library.library_name);
    }

    for (const auto& [path, libraries] : path_map) {
        if (libraries.size() > 1) {
            std::string conflict = "Path conflict for source path '" + path + "': ";
            for (size_t i = 0; i < libraries.size(); ++i) {
                if (i > 0) conflict += ", ";
                conflict += libraries[i];
            }
            conflicts.push_back(conflict);
        }
    }

    // Check for CMake target conflicts
    std::map<std::string, std::vector<std::string>> target_map;
    for (const auto& library : all_libraries) {
        for (const auto& target : library.cmake_targets) {
            target_map[target].push_back(library.library_name);
        }
    }

    for (const auto& [target, libraries] : target_map) {
        if (libraries.size() > 1) {
            std::string conflict = "CMake target conflict for '" + target + "': ";
            for (size_t i = 0; i < libraries.size(); ++i) {
                if (i > 0) conflict += ", ";
                conflict += libraries[i];
            }
            conflicts.push_back(conflict);
        }
    }

    return conflicts;
}

bool StandardManifestManager::resolve_conflicts(const std::vector<std::string>& conflicts) {
    // For now, just log the conflicts. A real implementation would have resolution strategies.
    for (const auto& conflict : conflicts) {
        // In a complete implementation, this would attempt to resolve conflicts
        // For now, we just acknowledge them
    }
    return true;
}

BuildConfiguration StandardManifestManager::get_build_configuration() {
    if (p_impl->manifests.empty()) {
        return BuildConfiguration{};
    }

    // Merge all build configurations
    std::vector<BuildConfiguration> configs;
    for (const auto& manifest : p_impl->manifests) {
        configs.push_back(manifest.build_config);
    }

    return merge_build_configurations(configs);
}

std::string StandardManifestManager::export_manifests(const std::string& format) {
    if (format == "json") {
        // Export all manifests as a combined JSON array
        std::ostringstream json;
        json << "[\n";

        for (size_t i = 0; i < p_impl->manifests.size(); ++i) {
            if (i > 0) json << ",\n";
            json << manifest_to_json(p_impl->manifests[i]);
        }

        json << "\n]";
        return json.str();
    } else if (format == "cmake") {
        return generate_cmake_configuration();
    }

    return "Unsupported export format: " + format;
}

bool StandardManifestManager::import_manifests(const std::string& content, const std::string& format) {
    if (format == "json") {
        // Try to parse as JSON array of manifests
        auto manifest = manifest_from_json(content);
        if (manifest) {
            return save_manifest(*manifest);
        }
    }

    return false;
}

// Utility function implementations
std::unique_ptr<ManifestManager> create_manifest_manager() {
    return std::make_unique<StandardManifestManager>();
}

std::string manifest_type_to_string(ManifestType type) {
    switch (type) {
        case ManifestType::LIBRARY_MANIFEST: return "LIBRARY_MANIFEST";
        case ManifestType::DEPENDENCY_MANIFEST: return "DEPENDENCY_MANIFEST";
        case ManifestType::PROJECT_MANIFEST: return "PROJECT_MANIFEST";
        case ManifestType::BUILD_MANIFEST: return "BUILD_MANIFEST";
        default: return "UNKNOWN";
    }
}

ManifestType string_to_manifest_type(const std::string& type_str) {
    if (type_str == "LIBRARY_MANIFEST") return ManifestType::LIBRARY_MANIFEST;
    if (type_str == "DEPENDENCY_MANIFEST") return ManifestType::DEPENDENCY_MANIFEST;
    if (type_str == "PROJECT_MANIFEST") return ManifestType::PROJECT_MANIFEST;
    if (type_str == "BUILD_MANIFEST") return ManifestType::BUILD_MANIFEST;
    return ManifestType::LIBRARY_MANIFEST; // Default
}

std::string generate_manifest_id(const std::string& library_name,
                                std::optional<std::chrono::system_clock::time_point> timestamp) {
    auto ts = timestamp ? *timestamp : std::chrono::system_clock::now();
    auto timestamp_value = std::chrono::duration_cast<std::chrono::seconds>(ts.time_since_epoch()).count();
    return "library_" + library_name + "_" + std::to_string(timestamp_value);
}

std::string manifest_to_json(const IntegrationManifest& manifest) {
    // Simple JSON serialization - in a real implementation, use a proper JSON library
    std::ostringstream json;

    auto time_to_string = [](std::chrono::system_clock::time_point tp) {
        auto time_t = std::chrono::system_clock::to_time_t(tp);
        std::ostringstream ss;
        ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%SZ");
        return ss.str();
    };

    json << "{\n";
    json << "    \"manifest_id\": \"" << manifest.manifest_id << "\",\n";
    json << "    \"manifest_type\": \"" << manifest_type_to_string(manifest.manifest_type) << "\",\n";
    json << "    \"schema_version\": \"" << manifest.schema_version << "\",\n";
    json << "    \"created_at\": \"" << time_to_string(manifest.created_at) << "\",\n";
    json << "    \"created_by\": \"" << manifest.created_by << "\",\n";
    json << "    \"description\": \"" << manifest.description << "\",\n";

    // Libraries
    json << "    \"libraries\": [\n";
    for (size_t i = 0; i < manifest.libraries.size(); ++i) {
        if (i > 0) json << ",\n";
        const auto& lib = manifest.libraries[i];
        json << "        {\n";
        json << "            \"library_name\": \"" << lib.library_name << "\",\n";
        json << "            \"library_version\": \"" << lib.library_version << "\",\n";
        json << "            \"origin_url\": \"" << lib.origin_url << "\",\n";
        json << "            \"commit_hash\": \"" << lib.commit_hash << "\",\n";
        json << "            \"source_path\": \"" << lib.source_path << "\",\n";
        json << "            \"build_path\": \"" << lib.build_path << "\",\n";
        json << "            \"is_enabled\": " << (lib.is_enabled ? "true" : "false") << ",\n";
        json << "            \"last_updated\": \"" << time_to_string(lib.last_updated) << "\"\n";
        json << "        }";
    }
    json << "\n    ],\n";

    // Build config
    json << "    \"build_config\": {\n";
    json << "        \"build_type\": \"" << manifest.build_config.build_type << "\",\n";
    json << "        \"toolchain_version\": \"" << manifest.build_config.toolchain_version << "\"\n";
    json << "    },\n";

    json << "    \"manifest_version\": \"" << manifest.manifest_version << "\"\n";
    json << "}";

    return json.str();
}

std::optional<IntegrationManifest> manifest_from_json(const std::string& json) {
    // Simplified JSON parsing - in a real implementation, use a proper JSON library
    IntegrationManifest manifest;

    // Extract basic fields using string search
    if (json.find("\"manifest_id\"") != std::string::npos) {
        size_t start = json.find("\"manifest_id\": \"") + 16;
        size_t end = json.find("\"", start);
        manifest.manifest_id = json.substr(start, end - start);
    }

    if (json.find("\"manifest_type\"") != std::string::npos) {
        size_t start = json.find("\"manifest_type\": \"") + 18;
        size_t end = json.find("\"", start);
        manifest.manifest_type = string_to_manifest_type(json.substr(start, end - start));
    }

    if (json.find("\"schema_version\"") != std::string::npos) {
        size_t start = json.find("\"schema_version\": \"") + 19;
        size_t end = json.find("\"", start);
        manifest.schema_version = json.substr(start, end - start);
    }

    if (json.find("\"created_by\"") != std::string::npos) {
        size_t start = json.find("\"created_by\": \"") + 15;
        size_t end = json.find("\"", start);
        manifest.created_by = json.substr(start, end - start);
    }

    if (json.find("\"description\"") != std::string::npos) {
        size_t start = json.find("\"description\": \"") + 16;
        size_t end = json.find("\"", start);
        manifest.description = json.substr(start, end - start);
    }

    if (json.find("\"manifest_version\"") != std::string::npos) {
        size_t start = json.find("\"manifest_version\": \"") + 20;
        size_t end = json.find("\"", start);
        manifest.manifest_version = json.substr(start, end - start);
    }

    // Default values for required fields
    if (manifest.manifest_id.empty() || manifest.schema_version.empty()) {
        return std::nullopt;
    }

    manifest.created_at = std::chrono::system_clock::now();

    return manifest;
}

std::string format_validation_result(const ValidationResult& result) {
    std::ostringstream formatted;

    formatted << "Validation Result: " << (result.is_valid ? "VALID" : "INVALID") << "\n";

    if (!result.errors.empty()) {
        formatted << "\nErrors (" << result.errors.size() << "):\n";
        for (const auto& error : result.errors) {
            formatted << "  - " << error << "\n";
        }
    }

    if (!result.warnings.empty()) {
        formatted << "\nWarnings (" << result.warnings.size() << "):\n";
        for (const auto& warning : result.warnings) {
            formatted << "  - " << warning << "\n";
        }
    }

    if (!result.info.empty()) {
        formatted << "\nInfo (" << result.info.size() << "):\n";
        for (const auto& info : result.info) {
            formatted << "  - " << info << "\n";
        }
    }

    return formatted.str();
}

std::string check_library_compatibility(const LibraryInfo& lib1, const LibraryInfo& lib2) {
    // Check for basic compatibility issues
    if (lib1.library_name == lib2.library_name) {
        if (lib1.library_version != lib2.library_version) {
            return "Version conflict: " + lib1.library_name + " " + lib1.library_version +
                   " vs " + lib2.library_version;
        }
    }

    if (lib1.source_path == lib2.source_path && lib1.library_name != lib2.library_name) {
        return "Source path conflict: " + lib1.library_name + " and " + lib2.library_name +
               " both use " + lib1.source_path;
    }

    return "Compatible";
}

BuildConfiguration merge_build_configurations(const std::vector<BuildConfiguration>& configs) {
    if (configs.empty()) {
        return BuildConfiguration{};
    }

    BuildConfiguration merged = configs[0];

    // Merge tool requirements
    std::set<std::string> all_tools;
    for (const auto& config : configs) {
        all_tools.insert(config.required_tools.begin(), config.required_tools.end());
    }
    merged.required_tools.assign(all_tools.begin(), all_tools.end());

    // Merge compiler flags
    std::set<std::string> all_flags;
    for (const auto& config : configs) {
        all_flags.insert(config.compiler_flags.begin(), config.compiler_flags.end());
    }
    merged.compiler_flags.assign(all_flags.begin(), all_flags.end());

    // Merge CMake definitions
    std::set<std::string> all_definitions;
    for (const auto& config : configs) {
        all_definitions.insert(config.cmake_definitions.begin(), config.cmake_definitions.end());
    }
    merged.cmake_definitions.assign(all_definitions.begin(), all_definitions.end());

    // Use most recent timestamp
    auto latest_time = std::chrono::system_clock::from_time_t(0);
    for (const auto& config : configs) {
        // In a real implementation, would track timestamps
    }

    return merged;
}

std::string normalize_version(const std::string& version_string) {
    // Remove 'v' prefix if present
    std::string normalized = version_string;
    if (!normalized.empty() && normalized[0] == 'v') {
        normalized = normalized.substr(1);
    }

    // Basic validation
    std::regex version_regex(R"(^(\d+)\.(\d+)\.(\d+).*$)");
    std::smatch matches;
    if (std::regex_match(normalized, matches, version_regex)) {
        return matches[1].str() + "." + matches[2].str() + "." + matches[3].str();
    }

    return normalized; // Return as-is if can't normalize
}

int compare_versions(const std::string& version1, const std::string& version2) {
    std::string v1 = normalize_version(version1);
    std::string v2 = normalize_version(version2);

    std::regex version_regex(R"(^(\d+)\.(\d+)\.(\d+)$)");
    std::smatch matches1, matches2;

    if (!std::regex_match(v1, matches1, version_regex) ||
        !std::regex_match(v2, matches2, version_regex)) {
        return 0; // Can't compare
    }

    // Compare major, minor, patch
    for (int i = 1; i <= 3; ++i) {
        int n1 = std::stoi(matches1[i].str());
        int n2 = std::stoi(matches2[i].str());

        if (n1 < n2) return -1;
        if (n1 > n2) return 1;
    }

    return 0; // Equal
}

} // namespace manifest
} // namespace integration