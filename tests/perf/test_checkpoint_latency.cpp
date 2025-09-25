#include <gtest/gtest.h>

TEST(CheckpointLatencyPerfTest, DISABLED_FailsWhenLatencyExceedsTwoSeconds) {
    // TODO: Measure checkpoint save/restore pipeline on NVMe and expect < 2000 ms.
    FAIL() << "Not implemented";
}
