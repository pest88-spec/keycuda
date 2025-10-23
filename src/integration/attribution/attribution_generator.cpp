// T021: Create Attribution Headers for All Extracted Files
// Generates comprehensive attribution headers with SPDX, copyright, and licensing information

#include "attribution_generator.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>

namespace integration {
namespace attribution {

AttributionGenerator::AttributionGenerator() {
    load_default_templates();
}

std::string AttributionGenerator::generate_header(const AttributionInfo& info, AttributionStyle style) {
    switch (style) {
        case AttributionStyle::HEADER_C:
            return generate_c_style_header(info);
        case AttributionStyle::HEADER_CPP:
            return generate_cpp_style_header(info);
        case AttributionStyle::HEADER_PYTHON:
            return generate_python_style_header(info);
        case AttributionStyle::HEADER_CMAKE:
            return generate_cmake_style_header(info);
        case AttributionStyle::HEADER_MD:
            return generate_markdown_style_header(info);
        case AttributionStyle::HEADER_TXT:
            return generate_text_style_header(info);
        default:
            return generate_c_style_header(info);
    }
}

bool AttributionGenerator::apply_attribution_to_file(const std::string& filepath,
                                                    const AttributionInfo& info,
                                                    AttributionStyle style) {
    if (should_skip_attribution(filepath)) {
        return true; // Skip but don't fail
    }

    if (!std::filesystem::exists(filepath)) {
        return false;
    }

    // Check if attribution already exists
    if (verify_attribution_exists(filepath)) {
        return true; // Already has attribution
    }

    // Auto-detect style if not specified
    if (style == AttributionStyle::HEADER_C) {
        style = detect_file_style(filepath);
    }

    // Generate attribution header
    std::string header = generate_header(info, style);

    // Read existing content
    std::string existing_content = read_file_content(filepath);
    if (existing_content.empty()) {
        return false;
    }

    // Combine header and existing content
    std::string new_content = header + "\n\n" + existing_content;

    // Write back to file
    return write_file_content(filepath, new_content);
}

bool AttributionGenerator::apply_attribution_to_directory(const std::string& dir_path,
                                                         const AttributionInfo& base_info,
                                                         bool recursive) {
    if (!std::filesystem::exists(dir_path) || !std::filesystem::is_directory(dir_path)) {
        return false;
    }

    bool success = true;

    try {
        if (recursive) {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(dir_path)) {
                if (entry.is_regular_file()) {
                    if (!apply_attribution_to_file(entry.path().string(), base_info)) {
                        success = false;
                    }
                }
            }
        } else {
            for (const auto& entry : std::filesystem::directory_iterator(dir_path)) {
                if (entry.is_regular_file()) {
                    if (!apply_attribution_to_file(entry.path().string(), base_info)) {
                        success = false;
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        return false;
    }

    return success;
}

bool AttributionGenerator::verify_attribution_exists(const std::string& filepath) {
    std::string content = read_file_content(filepath);
    if (content.empty()) {
        return false;
    }

    // Look for SPDX identifier
    return content.find("SPDX-License-Identifier:") != std::string::npos ||
           content.find("Attribution Notice") != std::string::npos ||
           content.find("Third-Party Code") != std::string::npos;
}

bool AttributionGenerator::verify_attribution_compliance(const std::string& filepath,
                                                        const AttributionInfo& expected_info) {
    if (!verify_attribution_exists(filepath)) {
        return false;
    }

    std::string content = read_file_content(filepath);

    // Check for required elements
    bool has_spdx = content.find("SPDX-License-Identifier:") != std::string::npos;
    bool has_library = content.find(expected_info.library_name) != std::string::npos;
    bool has_license = content.find(expected_info.license_type) != std::string::npos;
    bool has_source = content.find(expected_info.source_url) != std::string::npos;

    return has_spdx && has_library && has_license && has_source;
}

void AttributionGenerator::set_attribution_template(const std::string& template_content) {
    templates_[AttributionStyle::HEADER_C] = template_content;
}

void AttributionGenerator::add_custom_field(const std::string& name, const std::string& value) {
    custom_fields_[name] = value;
}

std::string AttributionGenerator::generate_c_style_header(const AttributionInfo& info) {
    std::stringstream ss;
    ss << "/*\n";
    ss << " * Attribution Notice for Integrated Third-Party Code\n";
    ss << " * \n";
    ss << " * Library: " << info.library_name << "\n";
    ss << " * Version: " << info.library_version << "\n";
    ss << " * Source: " << info.source_url << "\n";
    ss << " * Integrated: " << info.integration_date << "\n";
    ss << " * License: " << info.license_type << "\n";
    ss << " * SPDX-License-Identifier: " << info.spdx_license_id << "\n";

    if (!info.copyright_holder.empty()) {
        ss << " * Copyright: " << info.copyright_holder << "\n";
    }

    if (!info.original_authors.empty()) {
        ss << " * Authors: " << format_author_list(info.original_authors) << "\n";
    }

    ss << " * \n";
    ss << " * This file has been extracted from the upstream repository\n";
    ss << " * and integrated into this project with full attribution.\n";

    if (!info.modification_summary.empty()) {
        ss << " * Modifications: " << info.modification_summary << "\n";
    }

    ss << " * \n";
    ss << " * File integrity SHA256: " << info.file_hash_sha256 << "\n";

    // Add custom fields
    for (const auto& field : custom_fields_) {
        ss << " * " << field.first << ": " << field.second << "\n";
    }

    ss << " * \n";
    ss << " * For more information, see: " << info.source_url << "\n";
    ss << " */\n";

    return ss.str();
}

std::string AttributionGenerator::generate_cpp_style_header(const AttributionInfo& info) {
    // Similar to C-style but with additional C++ specific information
    return generate_c_style_header(info);
}

std::string AttributionGenerator::generate_python_style_header(const AttributionInfo& info) {
    std::stringstream ss;
    ss << "\"\"\"\n";
    ss << "Attribution Notice for Integrated Third-Party Code\n";
    ss << "\n";
    ss << "Library: " << info.library_name << "\n";
    ss << "Version: " << info.library_version << "\n";
    ss << "Source: " << info.source_url << "\n";
    ss << "Integrated: " << info.integration_date << "\n";
    ss << "License: " << info.license_type << "\n";
    ss << "SPDX-License-Identifier: " << info.spdx_license_id << "\n";

    if (!info.copyright_holder.empty()) {
        ss << "Copyright: " << info.copyright_holder << "\n";
    }

    ss << "\n";
    ss << "This file has been extracted from the upstream repository\n";
    ss << "and integrated into this project with full attribution.\n";
    ss << "\n";
    ss << "For more information, see: " << info.source_url << "\n";
    ss << "\"\"\"\n\n";

    return ss.str();
}

std::string AttributionGenerator::generate_cmake_style_header(const AttributionInfo& info) {
    std::stringstream ss;
    ss << "# Attribution Notice for Integrated Third-Party Code\n";
    ss << "#\n";
    ss << "# Library: " << info.library_name << "\n";
    ss << "# Version: " << info.library_version << "\n";
    ss << "# Source: " << info.source_url << "\n";
    ss << "# Integrated: " << info.integration_date << "\n";
    ss << "# License: " << info.license_type << "\n";
    ss << "# SPDX-License-Identifier: " << info.spdx_license_id << "\n";
    ss << "#\n";
    ss << "# This file has been extracted from the upstream repository\n";
    ss << "# and integrated into this project with full attribution.\n";
    ss << "#\n";
    ss << "# For more information, see: " << info.source_url << "\n";
    ss << "\n";

    return ss.str();
}

std::string AttributionGenerator::generate_markdown_style_header(const AttributionInfo& info) {
    std::stringstream ss;
    ss << "---\n";
    ss << "title: Attribution Notice\n";
    ss << "library: " << info.library_name << "\n";
    ss << "version: " << info.library_version << "\n";
    ss << "source: " << info.source_url << "\n";
    ss << "integrated: " << info.integration_date << "\n";
    ss << "license: " << info.license_type << "\n";
    ss << "spdx-license-id: " << info.spdx_license_id << "\n";

    if (!info.copyright_holder.empty()) {
        ss << "copyright: " << info.copyright_holder << "\n";
    }

    ss << "---\n\n";
    ss << "# Attribution Notice for Integrated Third-Party Code\n\n";
    ss << "This file contains code extracted from **" << info.library_name << "** version " << info.library_version << ".\n\n";
    ss << "**Source:** [" << info.source_url << "](" << info.source_url << ")\n\n";
    ss << "**License:** " << info.license_type << " (SPDX-License-Identifier: " << info.spdx_license_id << ")\n\n";
    ss << "**Integration Date:** " << info.integration_date << "\n\n";
    ss << "This file has been integrated into this project with full attribution and compliance with the original license terms.\n\n";

    return ss.str();
}

std::string AttributionGenerator::generate_text_style_header(const AttributionInfo& info) {
    std::stringstream ss;
    ss << "================================================================================\n";
    ss << "ATTRIBUTION NOTICE FOR INTEGRATED THIRD-PARTY CODE\n";
    ss << "================================================================================\n";
    ss << "\n";
    ss << "Library:        " << info.library_name << "\n";
    ss << "Version:        " << info.library_version << "\n";
    ss << "Source:         " << info.source_url << "\n";
    ss << "Integrated:     " << info.integration_date << "\n";
    ss << "License:        " << info.license_type << "\n";
    ss << "SPDX-License-Identifier: " << info.spdx_license_id << "\n";

    if (!info.copyright_holder.empty()) {
        ss << "Copyright:      " << info.copyright_holder << "\n";
    }

    ss << "\n";
    ss << "This file has been extracted from the upstream repository and integrated\n";
    ss << "into this project with full attribution and compliance with the original\n";
    ss << "license terms.\n";
    ss << "\n";
    ss << "For more information, see: " << info.source_url << "\n";
    ss << "================================================================================\n";
    ss << "\n";

    return ss.str();
}

std::string AttributionGenerator::get_current_timestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

std::string AttributionGenerator::format_author_list(const std::vector<std::string>& authors) const {
    std::stringstream ss;
    for (size_t i = 0; i < authors.size(); ++i) {
        if (i > 0) {
            if (i == authors.size() - 1) {
                ss << " and ";
            } else {
                ss << ", ";
            }
        }
        ss << authors[i];
    }
    return ss.str();
}

std::string AttributionGenerator::escape_comment_string(const std::string& content, AttributionStyle style) const {
    // Escape content based on comment style
    switch (style) {
        case AttributionStyle::HEADER_C:
        case AttributionStyle::HEADER_CPP:
            return content; // Already formatted in generate_*_style_header
        case AttributionStyle::HEADER_PYTHON:
            return content; // Already formatted
        case AttributionStyle::HEADER_CMAKE:
            return content; // Already formatted
        default:
            return content;
    }
}

std::string AttributionGenerator::detect_file_style(const std::string& filepath) const {
    std::filesystem::path path(filepath);
    std::string extension = path.extension().string();

    std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);

    if (extension == ".c" || extension == ".h") {
        return AttributionStyle::HEADER_C;
    } else if (extension == ".cpp" || extension == ".cxx" || extension == ".cc" ||
               extension == ".hpp" || extension == ".hxx") {
        return AttributionStyle::HEADER_CPP;
    } else if (extension == ".py") {
        return AttributionStyle::HEADER_PYTHON;
    } else if (extension == ".cmake" || path.filename() == "CMakeLists.txt") {
        return AttributionStyle::HEADER_CMAKE;
    } else if (extension == ".md") {
        return AttributionStyle::HEADER_MD;
    } else {
        return AttributionStyle::HEADER_C; // Default to C-style
    }
}

bool AttributionGenerator::should_skip_attribution(const std::string& filepath) const {
    std::filesystem::path path(filepath);
    std::string filename = path.filename().string();

    // Skip binary files and generated files
    std::vector<std::string> skip_extensions = {
        ".o", ".a", ".so", ".dll", ".exe", ".bin", ".obj", ".lib",
        ".png", ".jpg", ".jpeg", ".gif", ".bmp", ".ico", ".svg",
        ".pdf", ".zip", ".tar", ".gz", ".7z", ".rar"
    };

    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);

    for (const auto& skip_ext : skip_extensions) {
        if (extension == skip_ext) {
            return true;
        }
    }

    // Skip files that already have attribution
    if (verify_attribution_exists(filepath)) {
        return true;
    }

    return false;
}

std::string AttributionGenerator::read_file_content(const std::string& filepath) const {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        return "";
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    file.close();

    return buffer.str();
}

bool AttributionGenerator::write_file_content(const std::string& filepath, const std::string& content) const {
    std::ofstream file(filepath);
    if (!file.is_open()) {
        return false;
    }

    file << content;
    file.close();

    return true;
}

std::string AttributionGenerator::apply_template(const std::string& template_str, const AttributionInfo& info) const {
    std::string result = template_str;

    // Replace template variables
    result.replace(result.find("${LIBRARY_NAME}"), std::string("${LIBRARY_NAME}").length(), info.library_name);
    result.replace(result.find("${LIBRARY_VERSION}"), std::string("${LIBRARY_VERSION}").length(), info.library_version);
    result.replace(result.find("${SOURCE_URL}"), std::string("${SOURCE_URL}").length(), info.source_url);
    result.replace(result.find("${INTEGRATION_DATE}"), std::string("${INTEGRATION_DATE}").length(), info.integration_date);
    result.replace(result.find("${LICENSE_TYPE}"), std::string("${LICENSE_TYPE}").length(), info.license_type);
    result.replace(result.find("${SPDX_LICENSE_ID}"), std::string("${SPDX_LICENSE_ID}").length(), info.spdx_license_id);
    result.replace(result.find("${COPYRIGHT_HOLDER}"), std::string("${COPYRIGHT_HOLDER}").length(), info.copyright_holder);
    result.replace(result.find("${FILE_HASH_SHA256}"), std::string("${FILE_HASH_SHA256}").length(), info.file_hash_sha256);

    return result;
}

void AttributionGenerator::load_default_templates() {
    if (templates_loaded_) {
        return;
    }

    // Load default templates for different styles
    templates_[AttributionStyle::HEADER_C] = "";
    templates_[AttributionStyle::HEADER_CPP] = "";
    templates_[AttributionStyle::HEADER_PYTHON] = "";
    templates_[AttributionStyle::HEADER_CMAKE] = "";
    templates_[AttributionStyle::HEADER_MD] = "";
    templates_[AttributionStyle::HEADER_TXT] = "";

    templates_loaded_ = true;
}

// Global instance
static std::unique_ptr<AttributionGenerator> g_attribution_generator;

AttributionGenerator& get_attribution_generator() {
    if (!g_attribution_generator) {
        g_attribution_generator = std::make_unique<AttributionGenerator>();
    }
    return *g_attribution_generator;
}

// Convenience functions
bool create_secp256k1_attribution(const std::string& filepath,
                                 const std::string& version,
                                 const std::string& source_url) {
    AttributionInfo info;
    info.library_name = "secp256k1-zkp";
    info.library_version = version.empty() ? "master" : version;
    info.source_url = source_url;
    info.license_type = "MIT";
    info.spdx_license_id = "MIT";
    info.integration_date = get_current_timestamp();
    info.copyright_holder = "The Bitcoin Core Developers";
    info.modification_summary = "Namespace adaptation for integration";

    auto& generator = get_attribution_generator();
    return generator.apply_attribution_to_file(filepath, info);
}

bool apply_batch_attribution(const std::vector<std::string>& filepaths,
                            const AttributionInfo& info) {
    bool success = true;
    auto& generator = get_attribution_generator();

    for (const auto& filepath : filepaths) {
        if (!generator.apply_attribution_to_file(filepath, info)) {
            success = false;
        }
    }

    return success;
}

} // namespace attribution
} // namespace integration