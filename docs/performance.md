# Puzzle71Solver Performance Notes

## Baseline Targets
| GPU Model | Minimum Throughput (keys/sec) | Notes |
|-----------|-------------------------------|-------|
| RTX 2080 Ti | ≥ 1.0e9 | alerts when <1.0e9 |
| RTX 3090 | ≥ 2.0e9 | target variance ≤5% |
| NVIDIA A100 | ≥ 4.0e9 | maintain <128 regs/thread |

## Benchmark Procedure (Future Implementation)
1. Run `scripts/run-benchmarks.sh <devices> <samples>` (stub currently prints placeholder).
2. After script实现，将输出 `benchmarks/latest.json` with:
   - GPU metadata (model, driver, CUDA version)
   - Per-device throughput samples
   - Median/variance vs baseline
3. Review `metrics/puzzle71.prom` for last-run status + throughput gauges.
4. For detailed kernel metrics, execute `tools/nsight/puzzle71_profile.sh --device <id>` (stub pending real nsight command).

## TODOs
- Wire benchmark脚本 to invoke fused kernel, record 3 warmups + 5 samples.
- Integrate Nsight CLI into profile脚本, copy reports to `docs/perf/`.
- Append results + digest to final run report (`scripts/generate-report.sh`).
