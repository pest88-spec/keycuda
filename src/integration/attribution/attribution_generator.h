// T021: Create Attribution Headers for All Extracted Files
// Generates comprehensive attribution headers with SPDX, copyright, and licensing information

#pragma once

#include <string>
#include <vector>
#include <map>
#include <nlohmann/json.hpp>

namespace integration {
namespace attribution {

enum class AttributionStyle {
    HEADER_C,
    HEADER_CPP,
    HEADER_PYTHON,
    HEADER_CMAKE,
    HEADER_MD,
    HEADER_TXT
};

struct AttributionInfo {
    std::string library_name;
    std::string library_version;
    std::string source_url;
    std::string license_type;
    std::string copyright_holder;
    std::string integration_date;
    std::string modification_summary;
    std::vector<std::string> original_authors;
    std::map<std::string, std::string> custom_fields;
    std::string file_hash_sha256;
    std::string spdx_license_id;
};

class AttributionGenerator {
public:
    AttributionGenerator() = default;
    ~AttributionGenerator() = default;

    // Main attribution generation methods
    std::string generate_header(const AttributionInfo& info, AttributionStyle style);
    bool apply_attribution_to_file(const std::string& filepath,
                                  const AttributionInfo& info,
                                  AttributionStyle style = AttributionStyle::HEADER_C);

    // Batch operations
    bool apply_attribution_to_directory(const std::string& dir_path,
                                       const AttributionInfo& base_info,
                                       bool recursive = true);

    // Verification
    bool verify_attribution_exists(const std::string& filepath);
    bool verify_attribution_compliance(const std::string& filepath,
                                      const AttributionInfo& expected_info);

    // Configuration
    void set_attribution_template(const std::string& template_content);
    void add_custom_field(const std::string& name, const std::string& value);

private:
    // Style-specific generators
    std::string generate_c_style_header(const AttributionInfo& info);
    std::string generate_cpp_style_header(const AttributionInfo& info);
    std::string generate_python_style_header(const AttributionInfo& info);
    std::string generate_cmake_style_header(const AttributionInfo& info);
    std::string generate_markdown_style_header(const AttributionInfo& info);
    std::string generate_text_style_header(const AttributionInfo& info);

    // Helper methods
    std::string get_current_timestamp() const;
    std::string format_author_list(const std::vector<std::string>& authors) const;
    std::string escape_comment_string(const std::string& content, AttributionStyle style) const;
    std::string detect_file_style(const std::string& filepath) const;
    bool should_skip_attribution(const std::string& filepath) const;
    std::string read_file_content(const std::string& filepath) const;
    bool write_file_content(const std::string& filepath, const std::string& content) const;

    // Template system
    std::string apply_template(const std::string& template_str, const AttributionInfo& info) const;
    void load_default_templates();

    // Member variables
    std::map<AttributionStyle, std::string> templates_;
    std::map<std::string, std::string> custom_fields_;
    bool templates_loaded_ = false;
};

// Global accessor
AttributionGenerator& get_attribution_generator();

// Convenience functions
bool create_secp256k1_attribution(const std::string& filepath,
                                 const std::string& version = "",
                                 const std::string& source_url = "https://github.com/BlockstreamResearch/secp256k1-zkp");

bool apply_batch_attribution(const std::vector<std::string>& filepaths,
                            const AttributionInfo& info);

} // namespace attribution
} // namespace integration