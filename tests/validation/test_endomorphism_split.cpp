#include <gtest/gtest.h>

TEST(EndomorphismSplitTest, GpuMatchesCpuScalarSplit) {
    GTEST_SKIP() << "Requires GPU endomorphism kernels; deferred to parity suite";
}
