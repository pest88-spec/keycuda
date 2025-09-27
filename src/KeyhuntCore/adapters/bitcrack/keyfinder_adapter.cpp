#include "KeyhuntCore/adapters/bitcrack/keyfinder_adapter.h"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <limits>
#include <set>
#include <stdexcept>

#include "KeyhuntCore/adapters/bitcrack/conversions.h"
#include "puzzle71_kernel.h"
#include "CudaKeySearchDevice/CudaKeySearchDevice.h"
#include "KeyFinderLib/KeySearchDevice.h"
#include "KeyFinderLib/KeySearchTypes.h"

namespace {

constexpr std::uint64_t kMaxPointsPerThread = 4096;
constexpr std::uint32_t kCudaDeviceIndex = 0;

class KernelRunner {
public:
    void Initialize(const puzzle71::core::UInt256& start,
                    const puzzle71::core::UInt256& end,
                    std::uint64_t batch_size,
                    bool compressed,
                    const std::array<std::uint32_t,5>& target_hash) {
        Shutdown();
        start_ = start;
        current_ = start;
        end_ = end;
        compressed_ = compressed;
        target_hash_ = target_hash;
        desired_batch_size_ = batch_size == 0 ? 1 : batch_size;
        ConfigureDevice(desired_batch_size_);
        configured_ = true;
    }

    std::vector<bitcrack_adapter::KeySearchResult> RunStep() {
        if (!configured_) {
            return {};
        }

        auto remaining = puzzle71::core::Difference(end_, current_);
        remaining.AddUint64(1);
        std::uint64_t remaining64 = std::numeric_limits<std::uint64_t>::max();
        bool remaining_fits = false;
        if (remaining.FitsInUint64()) {
            remaining64 = remaining.ToUint64();
            remaining_fits = true;
        }

        if (!device_ || (remaining_fits && remaining64 < keys_per_step_)) {
            std::uint64_t desired = remaining_fits ? remaining64 : desired_batch_size_;
            ConfigureDevice(desired == 0 ? 1 : desired);
        }

        if (!device_) {
            configured_ = false;
            return {};
        }

        // Attempt GPU execution with basic error handling
        try {
            device_->doStep();
        } catch (const KeySearchException& ex) {
            const char* message = ex.msg.empty() ? "<no message>" : ex.msg.c_str();
            fprintf(stderr, "KeySearchException during GPU step: %s\n", message);
            fprintf(stderr, "This may indicate GPU resource constraints or configuration issues.\n");
            fprintf(stderr, "Consider reducing workload or checking GPU memory usage.\n");
            throw std::runtime_error(std::string("KeySearchException: ") + message);
        }

        std::vector<::KeySearchResult> bc_results;
        device_->getResults(bc_results);

        std::vector<bitcrack_adapter::KeySearchResult> out;
        out.reserve(bc_results.size());
        for (const auto& bc : bc_results) {
            bitcrack_adapter::KeySearchResult converted{};
            converted.private_key = bitcrack_adapter::FromBitCrack(bc.privateKey);
            converted.x = bitcrack_adapter::FromBitCrack(bc.publicKey.x);
            converted.y = bitcrack_adapter::FromBitCrack(bc.publicKey.y);
            converted.is_compressed = bc.compressed;
            for (std::size_t i = 0; i < converted.digest.size(); ++i) {
                converted.digest[i] = bc.hash[i];
            }
            out.push_back(std::move(converted));
        }

        auto current_before = current_;
        std::uint64_t produced = keys_per_step_;
        if (remaining_fits && remaining64 < produced) {
            produced = remaining64;
        }
        puzzle71::core::UInt256 next_scalar = puzzle71::core::Incremented(current_before, produced);

        if (remaining_fits && produced >= remaining64) {
            current_ = puzzle71::core::Incremented(end_, 1);
            configured_ = false;
            last_keys_processed_ = produced;
            last_next_scalar_ = current_;
            return out;
        }

        current_ = next_scalar;
        last_keys_processed_ = produced;
        last_next_scalar_ = current_;
        return out;
    }

    void Shutdown() {
        device_.reset();
        configured_ = false;
        keys_per_step_ = 0;
        last_keys_processed_ = 0;
        last_next_scalar_ = puzzle71::core::UInt256::Zero();
    }

    std::uint64_t LastKeysProcessed() const { return last_keys_processed_; }
    const puzzle71::core::UInt256& NextScalar() const { return last_next_scalar_; }
    bool HasPendingWork() const {
        if (!configured_) {
            return false;
        }
        return current_.Compare(end_) <= 0;
    }

private:
    void ConfigureDevice(std::uint64_t desired) {
        device_.reset();

        // Get GPU device properties for optimal configuration
        cudaDeviceProp device_props;
        cudaError_t err = cudaGetDeviceProperties(&device_props, kCudaDeviceIndex);
        if (err != cudaSuccess) {
            // Fallback to conservative defaults if device query fails
            device_props.multiProcessorCount = 28;  // RTX 2080 Ti default
            device_props.maxThreadsPerBlock = 1024;
            device_props.maxThreadsPerMultiProcessor = 2048;
            device_props.maxGridSize[0] = 2147483647;
            device_props.sharedMemPerBlock = 49152;
        }

        // Use conservative block size selection to avoid resource limits
        // "too many resources requested for launch" is caused by excessive register usage
        // Default to smaller block size that fits within GPU resource constraints
        unsigned int block_size = 256;  // Conservative: 256 threads (8 warps)

        // If device properties are available, try to optimize further
        if (err == cudaSuccess) {
            // Ensure block size doesn't exceed device limits
            if (block_size > (unsigned int)device_props.maxThreadsPerBlock) {
                block_size = (unsigned int)device_props.maxThreadsPerBlock;
                // Round down to nearest multiple of 32
                block_size = (block_size / 32) * 32;
            }

            // Conservative optimization to avoid "too many resources requested for launch"
            // BitCrack kernels use many registers for 256-bit integer operations
            unsigned int sm_count = device_props.multiProcessorCount;

            // Use very conservative block sizes to avoid all resource issues
            unsigned int test_block_sizes[] = {64, 96, 128, 192, 256};
            unsigned int num_test_sizes = sizeof(test_block_sizes) / sizeof(test_block_sizes[0]);

            // Start with very conservative size and increase if possible
            for (unsigned int i = 0; i < num_test_sizes; i++) {
                unsigned int test_size = test_block_sizes[i];
                if (test_size <= (unsigned int)device_props.maxThreadsPerBlock) {
                    // Ensure 32-alignment
                    test_size = (test_size / 32) * 32;
                    if (test_size >= 32) {  // Minimum 32 for BitCrack
                        block_size = test_size;
                        break;
                    }
                }
            }
        }

        // Align block size to 32 (BitCrack requirement)
        block_size = (block_size / 32) * 32;
        if (block_size < 32) {
            block_size = 32;
        }

        printf("CUDA Block Size Optimization:\n");
        printf("  Final Block Size: %u (32-aligned)\n", block_size);
        printf("  Device Query: %s\n", err == cudaSuccess ? "SUCCESS" : "FAILED");

        // Calculate optimal grid size to fully utilize all SMs
        // Each SM can handle multiple blocks concurrently
        unsigned int sm_count = device_props.multiProcessorCount;
        unsigned int max_blocks_per_sm = device_props.maxThreadsPerMultiProcessor / (unsigned int)block_size;
        unsigned int optimal_blocks = sm_count * max_blocks_per_sm;

        // Ensure we don't exceed grid limits but maximize GPU utilization
        std::uint64_t blocks = std::min<std::uint64_t>(optimal_blocks,
                                                      static_cast<std::uint64_t>(device_props.maxGridSize[0]));

        // Adjust blocks to handle the desired workload efficiently
        std::uint64_t threads_per_launch = blocks * (unsigned int)block_size;
        std::uint64_t target_points = 1;  // Start with 1 point per thread for maximum parallelism

        // Increase points per thread to compensate for smaller block size
        // This reduces kernel launch overhead and maintains total throughput
        if (desired > threads_per_launch * 8) {
            target_points = std::min<std::uint64_t>(desired / threads_per_launch, 256ULL);
        } else if (desired > threads_per_launch * 4) {
            target_points = std::min<std::uint64_t>(desired / threads_per_launch, 128ULL);
        } else if (desired > threads_per_launch * 2) {
            target_points = std::min<std::uint64_t>(desired / threads_per_launch, 64ULL);
        }

        // Adjust threads to match desired workload
        if (desired > 0 && desired < threads_per_launch) {
            // For small workloads, reduce blocks but maintain efficiency
            blocks = (desired + (unsigned int)block_size * target_points - 1) / ((unsigned int)block_size * target_points);
            blocks = std::max<std::uint64_t>(blocks, 1ULL);  // At least 1 block
            threads_per_launch = blocks * (unsigned int)block_size;
        }

        // Ensure reasonable points per thread (not too high, not too low)
        target_points = std::max<std::uint64_t>(target_points, 1ULL);
        target_points = std::min<std::uint64_t>(target_points, kMaxPointsPerThread);

        auto cfg = puzzle71::kernel::ChooseLaunchConfig(desired == 0 ? 1 : desired);
        cfg.block = dim3((unsigned int)block_size, 1, 1);
        cfg.grid = dim3(static_cast<unsigned int>(blocks), 1, 1);
        cfg.batch_size = threads_per_launch;

        points_per_thread_ = static_cast<int>(target_points);
        if (points_per_thread_ <= 0) {
            points_per_thread_ = 1;
        }

        // Log detailed GPU configuration for performance analysis and debugging
        printf("\n=== GPU Launch Configuration ===\n");
        printf("Device: %s\n", device_props.name);
        printf("SM Count: %d\n", device_props.multiProcessorCount);
        printf("Max Threads Per Block: %d\n", device_props.maxThreadsPerBlock);
        printf("Max Threads Per SM: %d\n", device_props.maxThreadsPerMultiProcessor);
        printf("\nLaunch Parameters:\n");
        printf("  Block Size: %d (32-aligned)\n", block_size);
        printf("  Grid Size: %llu\n", (unsigned long long)blocks);
        printf("  Total Threads: %llu\n", (unsigned long long)threads_per_launch);
        printf("  Points Per Thread: %d\n", points_per_thread_);
        printf("  Keys Per Step: %llu\n", (unsigned long long)(threads_per_launch * points_per_thread_));
        printf("  Desired Workload: %llu\n", (unsigned long long)desired);
        printf("  Batch Size: %lu\n", threads_per_launch);
        printf("\nResource Utilization:\n");
        printf("  SM Utilization: %.2f%%\n", (blocks * block_size * 100.0) / (sm_count * device_props.maxThreadsPerMultiProcessor));
        printf("  Memory Efficiency: %d points per thread\n", points_per_thread_);
        printf("  Device Optimization: %s\n", err == cudaSuccess ? "ENABLED" : "FALLBACK");
        printf("========================================\n\n");

        device_ = std::make_unique<CudaKeySearchDevice>(kCudaDeviceIndex,
                                                        static_cast<int>(cfg.block.x),
                                                        points_per_thread_,
                                                        static_cast<int>(cfg.grid.x));

        secp256k1::uint256 stride(1);
        secp256k1::uint256 start_bc = bitcrack_adapter::ToBitCrack(current_);
        int compression_flag = compressed_ ? PointCompressionType::COMPRESSED : PointCompressionType::UNCOMPRESSED;
        device_->init(start_bc, compression_flag, stride);

        std::set<KeySearchTarget> targets;
        targets.insert(KeySearchTarget(target_hash_.data()));
        device_->setTargets(targets);

        launch_cfg_ = cfg;
        keys_per_step_ = device_->keysPerStep();
        if (keys_per_step_ == 0) {
            keys_per_step_ = threads_per_launch * static_cast<std::uint64_t>(points_per_thread_);
        }
        last_keys_processed_ = 0;
        last_next_scalar_ = current_;
    }

    bool configured_{false};
    puzzle71::core::UInt256 start_{};
    puzzle71::core::UInt256 current_{};
    puzzle71::core::UInt256 end_{};
    bool compressed_{true};
    std::array<std::uint32_t,5> target_hash_{};
    std::unique_ptr<CudaKeySearchDevice> device_;
    puzzle71::kernel::KernelLaunchConfig launch_cfg_{};
    std::uint64_t desired_batch_size_{0};
    std::uint64_t keys_per_step_{0};
    int points_per_thread_{1};
    std::uint64_t last_keys_processed_{0};
    puzzle71::core::UInt256 last_next_scalar_{};
};

KernelRunner g_runner;
bitcrack_adapter::StepMetrics g_last_metrics{};
std::vector<bitcrack_adapter::KeySearchResult> g_cached_results;

}  // namespace

namespace bitcrack_adapter {

void Initialize(const puzzle71::core::UInt256& start,
                const puzzle71::core::UInt256& end,
                std::uint64_t batch_size,
                bool compressed,
                const std::array<std::uint32_t,5>& target_hash) {
    g_runner.Initialize(start, end, batch_size, compressed, target_hash);
    g_cached_results.clear();
    g_last_metrics = {};
    g_last_metrics.next_scalar = start;
    g_last_metrics.work_remaining = true;
}

void RunStep() {
    g_cached_results = g_runner.RunStep();
    g_last_metrics.keys_processed = g_runner.LastKeysProcessed();
    g_last_metrics.next_scalar = g_runner.NextScalar();
    g_last_metrics.work_remaining = g_runner.HasPendingWork();
}

std::vector<KeySearchResult> FetchResults() {
    return g_cached_results;
}

const StepMetrics& LastStepMetrics() {
    return g_last_metrics;
}

bool HasWorkScheduled() {
    return g_runner.HasPendingWork();
}

void Shutdown() {
    g_runner.Shutdown();
    g_cached_results.clear();
    g_last_metrics = {};
}

}  // namespace bitcrack_adapter
