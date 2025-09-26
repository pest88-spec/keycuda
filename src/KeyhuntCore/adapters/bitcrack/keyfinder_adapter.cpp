#include "KeyhuntCore/adapters/bitcrack/keyfinder_adapter.h"

#include <algorithm>
#include <memory>
#include <limits>
#include <set>

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

        device_->doStep();

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

        // Calculate optimal block size based on GPU architecture
        unsigned int block_size = std::min(static_cast<unsigned int>(device_props.maxThreadsPerBlock), 1024U);

        // Calculate optimal grid size to fully utilize all SMs
        // Each SM can handle multiple blocks concurrently
        unsigned int sm_count = device_props.multiProcessorCount;
        unsigned int max_blocks_per_sm = device_props.maxThreadsPerMultiProcessor / block_size;
        unsigned int optimal_blocks = sm_count * max_blocks_per_sm;

        // Ensure we don't exceed grid limits but maximize GPU utilization
        std::uint64_t blocks = std::min<std::uint64_t>(optimal_blocks,
                                                      static_cast<std::uint64_t>(device_props.maxGridSize[0]));

        // Adjust blocks to handle the desired workload efficiently
        std::uint64_t threads_per_launch = blocks * block_size;
        std::uint64_t target_points = 1;  // Start with 1 point per thread for maximum parallelism

        // For large workloads, increase points per thread to reduce kernel launch overhead
        if (desired > threads_per_launch * 16) {
            target_points = std::min<std::uint64_t>(desired / threads_per_launch, 64ULL);
        }

        // Adjust threads to match desired workload
        if (desired > 0 && desired < threads_per_launch) {
            // For small workloads, reduce blocks but maintain efficiency
            blocks = (desired + block_size * target_points - 1) / (block_size * target_points);
            blocks = std::max<std::uint64_t>(blocks, 1ULL);  // At least 1 block
            threads_per_launch = blocks * block_size;
        }

        // Ensure reasonable points per thread (not too high, not too low)
        target_points = std::max<std::uint64_t>(target_points, 1ULL);
        target_points = std::min<std::uint64_t>(target_points, kMaxPointsPerThread);

        auto cfg = puzzle71::kernel::ChooseLaunchConfig(desired == 0 ? 1 : desired);
        cfg.block = dim3(block_size, 1, 1);
        cfg.grid = dim3(static_cast<unsigned int>(blocks), 1, 1);
        cfg.batch_size = threads_per_launch;

        points_per_thread_ = static_cast<int>(target_points);
        if (points_per_thread_ <= 0) {
            points_per_thread_ = 1;
        }

        // Log GPU configuration for performance analysis
        printf("GPU Configuration:\n");
        printf("  Device: %s\n", device_props.name);
        printf("  SM Count: %d\n", device_props.multiProcessorCount);
        printf("  Block Size: %d\n", block_size);
        printf("  Grid Size: %llu\n", (unsigned long long)blocks);
        printf("  Total Threads: %llu\n", (unsigned long long)threads_per_launch);
        printf("  Points Per Thread: %d\n", points_per_thread_);
        printf("  Keys Per Step: %llu\n", (unsigned long long)(threads_per_launch * points_per_thread_));
        printf("  Desired Workload: %llu\n", (unsigned long long)desired);

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
