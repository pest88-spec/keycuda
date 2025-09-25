#include <gtest/gtest.h>

#include <array>
#include <cstdint>

namespace {

constexpr std::array<std::uint32_t, 8> kExampleScalar = {
    0x12345678u, 0x9abcdef0u, 0xfedcba98u, 0x76543210u,
    0x0badc0deu, 0xfeedfaceu, 0x01234567u, 0x89abcdefu};

TEST(ScalarOpsPropertyTest, DISABLED_ScalarSplitMatchesCpuReference) {
    // TODO: Implement secp256k1 scalar split CPU/GPU parity checks.
    FAIL() << "Not implemented";
}

TEST(ScalarOpsPropertyTest, DISABLED_ScalarAdditionWrapsModuloCurveOrder) {
    // TODO: Implement scalar addition modulo curve order using CPU + GPU helpers.
    FAIL() << "Not implemented";
}

}  // namespace
