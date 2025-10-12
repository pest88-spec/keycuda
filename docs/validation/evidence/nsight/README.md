# Nsight Profiling Attempt (2025-10-01)

- **Command**: `tools/nsight/puzzle71_profile.sh --device 0`
- **Result**: Failed with `ERR_NVGPUCTRPERM` — WSL2 environment lacks NVIDIA GPU performance counter permissions.
- **Consequence**: Unable to capture `.ncu-rep` or register usage metrics required for T057. Requires host with enabled profiling permissions (see https://developer.nvidia.com/ERR_NVGPUCTRPERM).
- **Next Step**: Retry on native Linux host with admin-enabled profiling counters or request ops to grant `nvidia-smi -pm 1` & `nvidia-smi -ac` permissions per NVIDIA guidelines.
