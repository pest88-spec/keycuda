#!/usr/bin/env python3
"""
Fix struct redefinitions by renaming conflicting types to be file-specific
"""

import re

def fix_memory_coalescing_optimizer():
    """Rename AccessPatternType and AccessPattern in memory_coalescing_optimizer.h"""
    file_path = "src/ComputeCore/gpu/performance/memory_coalescing_optimizer.h"

    with open(file_path, 'r') as f:
        content = f.read()

    # Rename AccessPatternType to CoalescingAccessPatternType
    content = re.sub(r'\benum class AccessPatternType\b', 'enum class CoalescingAccessPatternType', content)
    content = re.sub(r'\bAccessPatternType\b', 'CoalescingAccessPatternType', content)

    # Rename AccessPattern to CoalescingAccessPattern
    content = re.sub(r'\bstruct AccessPattern\b', 'struct CoalescingAccessPattern', content)
    content = re.sub(r'\bAccessPattern\b', 'CoalescingAccessPattern', content)

    with open(file_path, 'w') as f:
        f.write(content)

    print(f"✓ Fixed {file_path}")

def fix_memory_prefetch_manager():
    """Rename conflicting types in memory_prefetch_manager.h"""
    file_path = "src/ComputeCore/gpu/performance/memory_prefetch_manager.h"

    with open(file_path, 'r') as f:
        content = f.read()

    # Rename PrefetchStrategy enum to PrefetchStrategyType
    content = re.sub(r'\benum class PrefetchStrategy\b', 'enum class PrefetchStrategyType', content)
    # Be careful to only replace the enum usage, not the struct from memory_optimizer.h
    # Replace in specific contexts
    lines = content.split('\n')
    new_lines = []
    in_enum_def = False

    for line in lines:
        if 'enum class PrefetchStrategyType' in line:
            in_enum_def = True
            new_lines.append(line)
        elif in_enum_def and '};' in line:
            in_enum_def = False
            new_lines.append(line)
        elif in_enum_def:
            new_lines.append(line)
        else:
            # Replace PrefetchStrategy references in the context of the enum
            if 'PrefetchStrategy' in line and 'ALWAYS_PREFETCH' in line:
                line = line.replace('PrefetchStrategy::', 'PrefetchStrategyType::')
            elif 'PrefetchStrategy' in line and '::' in line and any(x in line for x in ['ADAPTIVE', 'BANDWIDTH', 'LATENCY', 'PATTERN', 'PREDICTIVE', 'CONSERVATIVE', 'AGGRESSIVE', 'ALWAYS']):
                line = line.replace('PrefetchStrategy::', 'PrefetchStrategyType::')
            new_lines.append(line)

    content = '\n'.join(new_lines)

    # Now rename AccessPatternType to PrefetchAccessPatternType
    content = re.sub(r'\benum class AccessPatternType\b', 'enum class PrefetchAccessPatternType', content)
    content = re.sub(r'\bAccessPatternType\b', 'PrefetchAccessPatternType', content)

    # Rename AccessPattern to PrefetchAccessPattern
    content = re.sub(r'\bstruct AccessPattern\b', 'struct PrefetchAccessPattern', content)
    content = re.sub(r'\bAccessPattern\b', 'PrefetchAccessPattern', content)

    with open(file_path, 'w') as f:
        f.write(content)

    print(f"✓ Fixed {file_path}")

if __name__ == "__main__":
    print("Fixing struct redefinitions...")
    fix_memory_coalescing_optimizer()
    fix_memory_prefetch_manager()
    print("✓ All struct redefinitions fixed!")
