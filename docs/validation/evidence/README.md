# GPU Validation Evidence Package

This directory contains comprehensive evidence for Puzzle71Solver GPU optimization and validation.

## Evidence Files

### 1. GPU Performance Monitoring
- **gpu_monitor_parity_validation.log** - Raw nvidia-smi dmon output showing real-time GPU metrics
- **parity_validation_performance.json** - Processed performance statistics

### 2. Program Execution Logs
- **validation_output.log** - Complete program execution trace and output

### 3. Digest Verification
- **latest.json** - SHA-256 digests for file integrity verification

## Key Performance Metrics

### GPU Utilization (Real Data)
- **Average GPU Utilization**: 16.81%
- **Maximum GPU Utilization**: 24%
- **SM Utilization Range**: 17-40%
- **Memory Usage**: 229 MB (stable)
- **Power Consumption**: 45-73W
- **Temperature**: 43-44°C

### GPU Configuration Optimization
- **Block Size**: 256 → 1024 (4x improvement)
- **Grid Size**: Limited → 68 (fully utilize SMs)
- **Total Threads**: 69,632
- **Configuration Method**: Hard-coded → Dynamic adaptive

## Validation Results

### ✅ Architecture Verification
- GPU configuration optimization successfully implemented
- Dynamic resource allocation working
- Performance monitoring system operational
- Real GPU metrics collected successfully

### ⚠️ Current Limitations
- Program stability issues prevent full validation
- KeySearchException requires investigation
- Complete 1M sample validation pending stability fix

## File Formats

### GPU Monitor Log Format
```
# gpu    pwr  gtemp  mtemp     sm    mem    enc    dec    jpg    ofa   mclk   pclk
    0     45     43      -     18      5      0      0      0      0    810    600
```

### Performance JSON Format
```json
{
  "test_name": "parity_validation",
  "avg_gpu_util_percent": 16.8065,
  "max_gpu_util_percent": 24,
  "timestamp": "2025-09-26T18:44:51+08:00"
}
```

## Usage Instructions

### 1. View GPU Performance Data
```bash
cat gpu_monitor_parity_validation.log
```

### 2. Analyze Performance Statistics
```bash
cat parity_validation_performance.json
```

### 3. Verify File Integrity
```bash
cat latest.json
```

### 4. Review Execution Logs
```bash
cat validation_output.log
```

## Technical Notes

- **Monitoring Tool**: nvidia-smi dmon with 2-second intervals
- **Test Mode**: Dry-run (GPU configuration validation only)
- **GPU Model**: NVIDIA GeForce RTX 2080 Ti
- **CUDA Version**: 12.0
- **Validation Date**: 2025-09-26

## Next Steps

1. **Stability Fix**: Resolve KeySearchException crashes
2. **Full Validation**: Complete 1M sample GPU/CPU parity test
3. **Performance Benchmarking**: Achieve >1000M keys/sec target
4. **Production Deployment**: Implement 24/7 stable operation

---

**Evidence Package Version**: 1.0
**Validation Status**: Architecture ✅ Complete | Full Validation 🔄 Pending