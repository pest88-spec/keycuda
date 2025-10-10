# Dependency Conflict Detection System

This directory contains the comprehensive dependency conflict detection system for third-party library integrations, providing automated detection, analysis, and resolution of conflicts that arise during the integration process.

## Overview

The conflict detection system identifies potential integration issues before they become blocking problems, enabling smooth library integration with minimal manual intervention. It provides comprehensive analysis across multiple dimensions of potential conflicts.

## Components

### 1. conflict_detector.h/cpp

Core C++ implementation providing sophisticated conflict detection and resolution capabilities:

**Key Features:**
- **Multi-dimensional Conflict Detection**: Version mismatches, license incompatibilities, symbol clashes, file conflicts, and build system conflicts
- **C++ Specific Analysis**: Namespace conflicts, include path conflicts, template instantiation issues
- **Dependency Graph Analysis**: Circular dependency detection and missing dependency identification
- **License Compatibility Matrix**: Comprehensive license compatibility checking with configurable rules
- **Automatic Resolution Strategies**: Built-in resolution strategies for common conflict types
- **Performance Optimized**: Caching mechanisms and parallel processing for large codebases

**Conflict Types Detected:**
- Version conflicts between different library versions
- License incompatibility issues
- Symbol and function name clashes
- File name conflicts across libraries
- Build system configuration conflicts
- Circular dependencies
- Missing dependencies
- API incompatibilities
- Namespace conflicts
- Include path conflicts

**Usage Example:**
```cpp
#include "src/integration/conflict_detector.h"

// Create detector instance
integration::conflict_detection::ConflictDetector detector;

// Configure detection settings
integration::conflict_detection::ConflictDetectionConfig config;
config.enable_version_checking = true;
config.enable_license_checking = true;
config.enable_symbol_scanning = true;
detector.set_configuration(config);

// Detect all conflicts
auto conflicts = detector.detect_all_conflicts();

// Analyze specific library
auto library_conflicts = detector.detect_conflicts_for_library("secp256k1-zkp");

// Auto-resolve where possible
auto resolved = detector.auto_resolve_conflicts(conflicts);

// Generate report
std::string report = detector.generate_conflict_report(conflicts);
```

### 2. detect-conflicts.sh

Shell script providing command-line access to conflict detection functionality:

**Key Features:**
- **Command-line Interface**: Easy integration into build pipelines
- **Configurable Detection**: Enable/disable specific conflict types
- **JSON Reporting**: Structured output for integration with CI/CD systems
- **Timeout Protection**: Prevents hanging during large scans
- **Colored Output**: Human-readable output for manual inspection

**Usage Examples:**
```bash
# Basic conflict detection
./scripts/detect-conflicts.sh

# Comprehensive analysis with auto-resolution
./scripts/detect-conflicts.sh --auto-resolve --timeout 600

# Specific analysis types
./scripts/detect-conflicts.sh --no-license-check --no-symbol-scanning

# Verbose output for debugging
./scripts/detect-conflicts.sh --verbose

# Custom integration root
./scripts/detect-conflicts.sh --integration-root /path/to/libraries
```

**Command-line Options:**
- `-h, --help`: Show help message
- `-v, --verbose`: Enable verbose logging
- `--no-version-check`: Disable version conflict detection
- `--no-license-check`: Disable license conflict detection
- `--no-symbol-scanning`: Disable symbol conflict scanning
- `--no-build-analysis`: Disable build system analysis
- `--auto-resolve`: Enable automatic conflict resolution
- `--timeout <seconds>`: Set scan timeout (default: 300)
- `--integration-root <path>`: Set integration root directory

## Conflict Types and Detection Methods

### 1. Version Conflicts
**Detection**: Analyzes library versions from VERSION files, CMakeLists.txt, and configuration files
**Resolution**: Version selection, aliasing, or compatibility layer creation
**Impact**: Can cause API incompatibilities and runtime errors

### 2. License Conflicts
**Detection**: Comprehensive license compatibility matrix checking
**Resolution**: License selection, legal review, or library replacement
**Impact**: Legal compliance and distribution restrictions

### 3. Symbol Conflicts
**Detection**: Symbol extraction from source files using regex analysis
**Resolution**: Namespace qualification, symbol renaming, or static linking
**Impact**: Link-time errors and runtime symbol resolution issues

### 4. File Conflicts
**Detection**: File system analysis for duplicate filenames and paths
**Resolution**: File renaming, directory restructuring, or selective inclusion
**Impact**: Build system failures and file overwriting

### 5. Build System Conflicts
**Detection**: CMakeLists.txt analysis for conflicting options and configurations
**Resolution**: Configuration merging, conditional compilation, or build system refactoring
**Impact**: Build failures and configuration incompatibilities

### 6. Dependency Conflicts
**Detection**: Dependency graph analysis for cycles and missing dependencies
**Resolution**: Dependency refactoring, circular dependency breaking, or dependency injection
**Impact**: Build failures and runtime dependency resolution errors

### 7. Namespace Conflicts (C++ Specific)
**Detection**: Namespace declaration analysis in header files
**Resolution**: Namespace qualification, anonymous namespaces, or unique naming
**Impact**: Compilation errors and ambiguous symbol resolution

## Integration with Build Systems

### CMake Integration
Add conflict detection to your CMake build process:

```cmake
# Add custom target for conflict detection
add_custom_target(conflict-detection
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/detect-conflicts.sh
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running dependency conflict detection"
)

# Make conflict detection part of the build process
add_dependencies(your-target conflict-detection)
```

### CI/CD Pipeline Integration
GitHub Actions example:

```yaml
name: Conflict Detection
on: [push, pull_request]

jobs:
  conflict-detection:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v2
      - name: Run Conflict Detection
        run: ./scripts/detect-conflicts.sh --timeout 600
      - name: Upload Reports
        uses: actions/upload-artifact@v2
        with:
          name: conflict-reports
          path: build/conflict-reports/
```

## Configuration

### Detection Configuration
Fine-tune conflict detection behavior:

```cpp
integration::conflict_detection::ConflictDetectionConfig config;

// Enable/disable specific detection types
config.enable_version_checking = true;
config.enable_license_checking = true;
config.enable_symbol_scanning = true;
config.enable_build_analysis = true;
config.enable_dependency_graph_analysis = true;

// Configure timeouts and limits
config.max_scan_depth = 10;
config.scan_timeout = std::chrono::minutes(5);

// Set ignored conflicts
config.ignored_conflicts = {"symbol_clash_common_utils"};
```

### License Compatibility Matrix
Configure custom license compatibility rules:

```cpp
// Add custom license compatibility
config.license_compatibility_matrix["MyLicense"] = {"MIT", "BSD", "Apache-2.0"};
```

### Resolution Strategies
Define custom conflict resolution strategies:

```cpp
integration::conflict_detection::ConflictResolutionStrategy strategy;
strategy.strategy_name = "custom_resolution";
strategy.applicable_type = integration::conflict_detection::ConflictType::VERSION_MISMATCH;
strategy.automatic_resolution = true;
strategy.resolver = [](const auto& conflict) {
    // Custom resolution logic
    return true;
};
```

## Performance Considerations

### Optimization Strategies
- **Caching**: Symbol extraction and version detection results are cached
- **Parallel Processing**: Multiple libraries can be analyzed in parallel
- **Incremental Analysis**: Only analyze changed libraries when possible
- **Selective Scanning**: Enable/disable specific conflict types based on needs

### Memory Usage
- **Streaming Analysis**: Large files are processed in chunks
- **Symbol Filtering**: Only relevant symbols are extracted and analyzed
- **Cache Management**: Automatic cache cleanup and size limits

### Scalability
- **Large Codebases**: Optimized for projects with many integrated libraries
- **Incremental Updates**: Supports continuous integration workflows
- **Distributed Processing**: Can be integrated with distributed build systems

## Best Practices

### 1. Early Detection
- Run conflict detection early in the integration process
- Integrate with pre-commit hooks for early feedback
- Use in CI/CD pipelines for automated checking

### 2. Configuration Management
- Customize detection settings for your project needs
- Configure license compatibility matrix according to legal requirements
- Set appropriate timeouts for your project size

### 3. Resolution Planning
- Address blocking conflicts before proceeding with integration
- Document resolution decisions for future reference
- Use auto-resolution for common, well-understood conflicts

### 4. Monitoring and Reporting
- Track conflict trends over time
- Generate reports for compliance and documentation
- Monitor false positives and adjust configuration accordingly

## Troubleshooting

### Common Issues

1. **Timeout During Scanning**
   - Increase timeout with `--timeout` option
   - Disable resource-intensive checks with `--no-symbol-scanning`
   - Exclude large test directories from analysis

2. **False Positives**
   - Add specific conflicts to ignored conflicts list
   - Fine-tune detection configuration
   - Report systematic false positives for improvement

3. **Performance Issues**
   - Enable caching for repeated analyses
   - Use parallel processing where possible
   - Selectively enable only needed conflict types

4. **Integration Issues**
   - Ensure proper permissions for file system access
   - Check that required tools (find, grep, sed) are available
   - Verify integration root directory is correctly specified

### Debug Mode
Enable verbose output for troubleshooting:

```bash
./scripts/detect-conflicts.sh --verbose --timeout 600
```

### Report Analysis
Review generated reports for detailed conflict information:

```bash
# View latest report
cat build/conflict-reports/conflict-report-*.json | jq '.summary'

# Check specific conflict types
cat build/conflict-reports/conflict-report-*.json | jq '.conflicts[] | select(.type == "version_mismatch")'
```

## Contributing

When adding new conflict detection capabilities:

1. **Define Conflict Type**: Add new conflict type to the enumeration
2. **Implement Detection**: Add detection logic to appropriate methods
3. **Add Resolution**: Implement automatic resolution where possible
4. **Update Documentation**: Document new conflict types and resolution strategies
5. **Add Tests**: Include comprehensive test cases for new functionality
6. **Performance Testing**: Ensure scalability for large codebases

## Security Considerations

### File System Access
- Conflict detection reads only specified integration directories
- No modification of source files during detection
- Secure handling of file paths and permissions

### Code Analysis
- Symbol extraction uses safe regex patterns
- No code execution during analysis
- Input validation for all external inputs

### Report Generation
- Sensitive information is not included in reports
- Configurable report output locations
- Secure handling of temporary files

## License

This conflict detection system is part of the Puzzle71Solver project and is licensed under the MIT License.