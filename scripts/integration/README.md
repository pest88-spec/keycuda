# Integration Error Handling and Recovery System

This directory contains comprehensive error handling, recovery, and monitoring mechanisms for third-party library integration operations.

## Overview

The integration error handling system provides:

- **Automatic error detection** across all integration phases
- **Intelligent recovery strategies** with multiple fallback mechanisms
- **Health monitoring** with continuous status checking
- **State management** with backup and rollback capabilities
- **Performance tracking** with anomaly detection

## Components

### 1. error-handler.sh

Main error handling and recovery script with:

- **Error Classification**: Categorizes errors into types (extraction, attribution, build, dependency, etc.)
- **Recovery Strategies**: Implements multiple recovery approaches per error type
- **State Management**: Tracks integration state and provides rollback capabilities
- **Backup System**: Creates automatic backups before risky operations

**Key Features:**
- Intelligent retry with exponential backoff
- Fallback to previous versions
- Component skipping for problematic parts
- Alternative source resolution
- Graceful degradation mechanisms

**Usage:**
```bash
# Handle integration errors automatically
./error-handler.sh perform-integration secp256k1-zkp "extract_library secp256k1-zkp"

# Create manual backup
./error-handler.sh create-backup secp256k1-zkp

# Restore from backup
./error-handler.sh restore-backup /path/to/backup

# Validate integration state
./error-handler.sh validate-state secp256k1-zkp
```

### 2. recovery-strategies.sh

Specialized recovery strategies for different failure scenarios:

- **Intelligent Retry**: Retry operations with modified parameters based on failure analysis
- **Partial Integration**: Continue with incomplete integrations when possible
- **Component Substitution**: Replace missing components with alternatives
- **Graceful Degradation**: Implement reduced functionality when full integration fails
- **Alternative Sources**: Try different sources when primary source fails

**Key Features:**
- Context-aware retry logic
- Automatic component substitution
- Degraded implementation generation
- Alternative source resolution
- Recovery strategy selection

**Usage:**
```bash
# Select appropriate recovery strategy
./recovery-strategies.sh select-strategy secp256k1-zkp EXTRACTION_FAILED network_timeout

# Perform intelligent retry
./recovery-strategies.sh intelligent-retry "extract_library lib" network_timeout 5

# Attempt partial integration
./recovery-strategies.sh partial-integration secp256k1-zkp 0.7

# Substitute missing components
./recovery-strategies.sh component-substitution secp256k1-zkp build_system
```

### 3. health-check.sh

Comprehensive health monitoring and reporting system:

- **Integrity Checks**: Validates integration completeness and correctness
- **Build Health**: Monitors build system status and performance
- **Dependency Analysis**: Tracks dependency satisfaction and conflicts
- **Performance Metrics**: Monitors integration performance trends
- **Anomaly Detection**: Identifies unusual patterns or issues

**Key Features:**
- Automated health assessments
- Continuous monitoring capability
- Performance trend analysis
- Anomaly detection algorithms
- Comprehensive reporting

**Usage:**
```bash
# Generate health report
./health-check.sh generate-report
./health-check.sh generate-report secp256k1-zkp

# Start continuous monitoring
./health-check.sh monitor 3600

# Check specific aspects
./health-check.sh check-integrity secp256k1-zkp
./health-check.sh check-build secp256k1-zkp
./health-check.sh detect-anomalies secp256k1-zkp

# Show health summary
./health-check.sh show-summary
```

## Error Types and Recovery Strategies

### 1. Extraction Failures

**Error Types:**
- `EXTRACTION_FAILED`: Complete extraction failure
- `EXTRACTION_INCOMPLETE`: Partial extraction with missing components

**Recovery Strategies:**
1. Intelligent retry with modified parameters
2. Alternative source resolution
3. Partial integration with minimum completeness threshold
4. Component substitution for missing parts

### 2. Attribution Issues

**Error Types:**
- `ATTRIBUTION_FAILED`: Missing or incorrect attribution headers

**Recovery Strategies:**
1. Regenerate attribution headers
2. Automatic attribution detection and application
3. Fallback to minimal attribution

### 3. Build Integration Problems

**Error Types:**
- `BUILD_INTEGRATION_FAILED`: CMake configuration or build failures
- `DEPENDENCY_CONFLICT`: Version or dependency conflicts

**Recovery Strategies:**
1. Conflict resolution and mediation
2. Component exclusion
3. Alternative build system generation
4. Partial integration configuration

### 4. System Issues

**Error Types:**
- `DISK_SPACE_INSUFFICIENT`: Storage limitations
- `NETWORK_ERROR`: Network connectivity problems
- `LICENSE_INCOMPATIBLE`: License compatibility issues

**Recovery Strategies:**
1. Resource cleanup and optimization
2. Alternative network sources
3. Manual review workflows
4. Graceful degradation

## Configuration

### Environment Variables

- `INTEGRATION_ROOT`: Root directory for extracted libraries (default: `src/extracted`)
- `BACKUP_DIR`: Directory for integration backups (default: `.integration-backups`)
- `MAX_RETRY_ATTEMPTS`: Maximum retry attempts (default: 3)
- `HEALTH_CHECK_INTERVAL`: Monitoring interval in seconds (default: 3600)

### Thresholds

- `CRITICAL_THRESHOLD`: Health score threshold for critical issues (default: 0.9)
- `WARNING_THRESHOLD`: Health score threshold for warnings (default: 0.7)
- `PARTIAL_INTEGRATION_THRESHOLD`: Minimum completeness for partial integration (default: 0.8)
- `MAX_DISK_USAGE_PERCENT`: Disk usage warning threshold (default: 85%)

## Integration with Build System

### CMake Integration

The error handling system integrates with CMake through:

- **Configuration Hooks**: Pre-build checks and post-build validation
- **Error Propagation**: Build errors trigger recovery mechanisms
- **State Tracking**: Build state persistence across sessions

### Example CMake Integration

```cmake
# Pre-build health check
add_custom_target(pre-build-check
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/integration/health-check.sh check-integrity ${LIBRARY_NAME}
    COMMENT "Checking integration health before build"
)

# Post-build validation
add_custom_target(post-build-check
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/integration/health-check.sh check-build ${LIBRARY_NAME}
    DEPENDS ${LIBRARY_NAME}
    COMMENT "Validating build health after build"
)
```

## Monitoring and Alerting

### Health Reports

Health reports are generated in JSON format with comprehensive status information:

```json
{
  "timestamp": "2025-10-09T10:30:00+00:00",
  "libraries": [
    {
      "name": "secp256k1-zkp",
      "overall_status": "HEALTHY",
      "integrity": {
        "status": "HEALTHY",
        "message": "Integration integrity: 5/5 checks passed",
        "score": 1.0
      },
      "build": {
        "status": "HEALTHY",
        "message": "Build successful in 2.5 minutes"
      }
    }
  ],
  "system": {
    "disk_space": {
      "status": "HEALTHY",
      "message": "Disk usage: 45% (100GB available)",
      "usage_percent": 45
    }
  }
}
```

### Continuous Monitoring

For production environments, enable continuous monitoring:

```bash
# Monitor all libraries every hour
./health-check.sh monitor 3600

# Monitor specific library every 30 minutes
./health-check.sh monitor 1800 secp256k1-zkp
```

## Best Practices

### 1. Preventive Measures

- Run health checks before major operations
- Create regular backups during integration
- Monitor disk space and system resources
- Validate attribution completeness

### 2. Error Handling

- Always use the error handler for integration operations
- Implement appropriate retry strategies
- Document manual intervention requirements
- Use graceful degradation for non-critical failures

### 3. Recovery Planning

- Test recovery strategies regularly
- Maintain backup retention policies
- Document known issues and solutions
- Plan for manual intervention scenarios

### 4. Monitoring

- Set up automated health monitoring
- Configure appropriate alert thresholds
- Review health reports regularly
- Track performance trends over time

## Troubleshooting

### Common Issues

1. **Backup Creation Fails**
   - Check disk space availability
   - Verify permissions on backup directory
   - Ensure source directories exist

2. **Recovery Strategies Fail**
   - Review error logs for specific failure reasons
   - Check network connectivity for alternative sources
   - Verify system dependencies are installed

3. **Health Check Failures**
   - Examine specific check failure messages
   - Review integration completeness
   - Check build system configuration

4. **Performance Issues**
   - Monitor resource usage during operations
   - Review performance trends in health reports
   - Consider partial integration for large libraries

### Debug Mode

Enable verbose logging for debugging:

```bash
./error-handler.sh -v perform-integration library_name operation
./recovery-strategies.sh -v select-strategy library_name error_type context
./health-check.sh -v generate-report
```

## Contributing

When adding new error types or recovery strategies:

1. Update error classification in `error-handler.sh`
2. Implement recovery logic in `recovery-strategies.sh`
3. Add health checks in `health-check.sh`
4. Update documentation with new capabilities
5. Test with various failure scenarios

## License

This integration error handling system is part of the Puzzle71Solver project and is licensed under the MIT License.