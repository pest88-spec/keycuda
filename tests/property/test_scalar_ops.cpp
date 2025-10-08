#include <gtest/gtest.h>

#include <array>
#include <cstdint>

namespace {

TEST(ScalarOpsPropertyTest, ScalarSplitMatchesCpuReference) {
    GTEST_SKIP() << "Scalar split parity requires secp256k1 fixtures";
}

TEST(ScalarOpsPropertyTest, ScalarAdditionWrapsModuloCurveOrder) {
    GTEST_SKIP() << "Scalar arithmetic property checks deferred to parity harness";
}

}  // namespace
