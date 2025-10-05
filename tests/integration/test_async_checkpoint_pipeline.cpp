#include <cuda_runtime.h>
#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <string>

namespace {

class ScopedWorkingDirectory {
public:
    explicit ScopedWorkingDirectory(const std::filesystem::path& target)
        : original_(std::filesystem::current_path()) {
        std::filesystem::create_directories(target);
        std::filesystem::current_path(target);
    }

    ~ScopedWorkingDirectory() {
        try {
            std::filesystem::current_path(original_);
        } catch (...) {
        }
    }

private:
    std::filesystem::path original_;
};

std::filesystem::path ResolveSolverBinary() {
    const auto original = std::filesystem::current_path();
    const auto solver_path = original / "Puzzle71Solver";
#ifdef _WIN32
    return solver_path.replace_extension(".exe");
#else
    return solver_path;
#endif
}

}  // namespace

TEST(AsyncCheckpointPipelineTest, WritesPayloadAndManifestFiles) {
    int device_count = 0;
    auto cuda_status = cudaGetDeviceCount(&device_count);
    if (cuda_status != cudaSuccess || device_count <= 0) {
        GTEST_SKIP() << "Skipping: CUDA device not available";
    }

    const auto solver_binary = ResolveSolverBinary();
    ASSERT_TRUE(std::filesystem::exists(solver_binary)) << "Solver binary not found: " << solver_binary;

    const auto temp_root = std::filesystem::temp_directory_path() / "puzzle71_async_checkpoint";
    std::filesystem::remove_all(temp_root);

    ScopedWorkingDirectory scoped(temp_root);

    std::ostringstream command;
    command << '"' << solver_binary.string() << '"'
            << " --keyspace 0x1:0x1000"
            << " --target-address 1PWo3JeB9jrGwfHDNpdGK54CRas7fsVzXU"
            << " --operator-id async-test"
            << " --operator-purpose unit-test"
            << " --device 0"
            << " --enable-checkpoint"
            << " --super";

    int rc = std::system(command.str().c_str());
    ASSERT_EQ(rc, 0) << "Solver exited with code " << rc;

    const auto checkpoint_dir = std::filesystem::path("checkpoints");
    ASSERT_TRUE(std::filesystem::exists(checkpoint_dir));

    bool payload_found = false;
    bool manifest_found = false;
    for (const auto& entry : std::filesystem::directory_iterator(checkpoint_dir)) {
        if (entry.path().extension() == ".chk") {
            payload_found = true;
        }
        if (entry.path().extension() == ".json") {
            manifest_found = true;
        }
    }

    EXPECT_TRUE(payload_found);
    EXPECT_TRUE(manifest_found);

    std::filesystem::remove_all(temp_root);
}
