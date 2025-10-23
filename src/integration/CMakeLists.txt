# T001 & T002: Integration Infrastructure CMakeLists.txt
# This file sets up the integration infrastructure for third-party dependencies

# Find nlohmann_json (system or local)
find_package(nlohmann_json QUIET)

# Create integration library
add_library(integration STATIC)

# Set target properties
set_target_properties(integration PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
)

# Add integration source files
target_sources(integration PRIVATE
    metrics/metrics.cpp
    manifests/manifest_manager.cpp
    baseline/baseline_measurer.cpp
    attribution/attribution_verifier.cpp
    config/config_manager.cpp
    evidence/evidence_collector.cpp
    audit/audit_logger.cpp
)

# Add include directories
target_include_directories(integration PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# Link dependencies
if(nlohmann_json_FOUND)
    target_link_libraries(integration PUBLIC
        nlohmann_json::nlohmann_json
    )
endif()

# T003: Integration logging with JSON output
target_compile_definitions(integration PRIVATE
    INTEGRATION_ENABLE_LOGGING=$<BOOL:${INTEGRATION_TRACK_METRICS}>
    INTEGRATION_JSON_OUTPUT=$<BOOL:${INTEGRATION_TRACK_METRICS}>
)

# Installation
install(TARGETS integration
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

# Install integration headers
install(DIRECTORY metrics/ manifests/ baseline/ attribution/ config/ evidence/ audit/
    DESTINATION include/integration
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
)