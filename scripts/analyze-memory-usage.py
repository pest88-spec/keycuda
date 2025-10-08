#!/usr/bin/env python3
"""
Memory Usage Analysis Tool for Puzzle71Solver
Analyzes GPU memory usage patterns and suggests optimizations
"""

import json
import subprocess
import re
import sys
from datetime import datetime

def get_nvidia_smi_info():
    """Get current GPU memory and utilization info"""
    try:
        result = subprocess.run(['nvidia-smi', '--query-gpu=memory.total,memory.used,memory.free,utilization.gpu,temperature.gpu,power.draw',
                               '--format=csv,noheader,nounits'],
                              capture_output=True, text=True)
        if result.returncode == 0:
            return result.stdout.strip().split(', ')
    except Exception as e:
        print(f"Error getting nvidia-smi info: {e}")
    return None

def analyze_batch_config():
    """Analyze current batch configuration and memory usage"""
    gpu_info = get_nvidia_smi_info()
    if not gpu_info:
        print("Cannot get GPU info")
        return

    memory_total = int(gpu_info[0])  # MB
    memory_used = int(gpu_info[1])    # MB
    memory_free = int(gpu_info[2])    # MB
    gpu_util = int(gpu_info[3])       # %
    temperature = int(gpu_info[4])    # C
    power_draw = float(gpu_info[5])   # W

    print(f"\n=== GPU Memory and Utilization Analysis ===")
    print(f"GPU Memory Total: {memory_total} MB")
    print(f"GPU Memory Used:  {memory_used} MB ({memory_used/memory_total*100:.1f}%)")
    print(f"GPU Memory Free:  {memory_free} MB ({memory_free/memory_total*100:.1f}%)")
    print(f"GPU Utilization:  {gpu_util}%")
    print(f"GPU Temperature:  {temperature}°C")
    print(f"Power Draw:       {power_draw}W")

    # Read latest benchmark if available
    try:
        with open('benchmarks/latest.json', 'r') as f:
            benchmark = json.load(f)

        if 'stats' in benchmark:
            keys_per_sec = benchmark['stats'].get('mean', 0)
            print(f"Current Performance: {keys_per_sec/1_000_000:.2f} Mkeys/s")

            # Calculate efficiency metrics
            efficiency = keys_per_sec / max(memory_used, 1)  # keys per MB
            print(f"Memory Efficiency: {efficiency:.2f} keys/s per MB")

            # GPU utilization efficiency
            if gpu_util > 0:
                gpu_efficiency = keys_per_sec / gpu_util
                print(f"GPU Utilization Efficiency: {gpu_efficiency/1_000_000:.2f} Mkeys/s per % utilization")
    except Exception as e:
        print(f"Could not read benchmark data: {e}")

    # Memory optimization suggestions
    print(f"\n=== Memory Optimization Analysis ===")

    if memory_used < memory_total * 0.3:
        print("✓ Low memory usage - can increase batch sizes for better performance")
        suggested_increase = int((memory_total * 0.7 - memory_used))
        print(f"  Suggested: Increase memory usage by ~{suggested_increase} MB")
    elif memory_used > memory_total * 0.85:
        print("⚠ High memory usage - risk of OOM errors")
        print("  Suggested: Reduce batch sizes or optimize memory usage")
    else:
        print("✓ Memory usage is in optimal range (30-85%)")

    # GPU utilization analysis
    print(f"\n=== GPU Utilization Analysis ===")
    if gpu_util < 50:
        print("⚠ Low GPU utilization - consider:")
        print("  - Increasing batch size")
        print("  - Optimizing kernel launch configuration")
        print("  - Reducing memory transfer overhead")
    elif gpu_util > 90:
        print("✓ High GPU utilization - good performance")
    else:
        print("✓ Moderate GPU utilization - acceptable performance")

    # Temperature and power analysis
    print(f"\n=== Thermal and Power Analysis ===")
    if temperature > 80:
        print("⚠ High temperature - may cause thermal throttling")
    elif temperature < 50:
        print("✓ Low temperature - good thermal headroom")
    else:
        print("✓ Normal operating temperature")

    if power_draw < 50:
        print("⚠ Low power draw - GPU may be underutilized")
    elif power_draw > 120:
        print("⚠ High power draw - check cooling")
    else:
        print("✓ Normal power consumption")

def main():
    print(f"Puzzle71Solver Memory Usage Analysis - {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    print("="*60)

    analyze_batch_config()

if __name__ == "__main__":
    main()