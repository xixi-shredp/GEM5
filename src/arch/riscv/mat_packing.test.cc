#include <gtest/gtest.h>

#include <array>
#include <cstdint>

#include "arch/riscv/regs/mat.hh"

namespace gem5
{

namespace RiscvISA
{

TEST(MatPacking, SerializeWordRowLittleEndian)
{
    MatRegContainer reg;
    auto row = getHSlice<uint32_t>(reg, 0);
    row[0] = 0x11223344u;
    row[1] = 0x55667788u;
    row[2] = 0x99aabbccu;
    row[3] = 0xddeeff00u;

    const auto bytes = serializeMatRowBytes(reg, 0);
    const std::array<uint8_t, MatRowByteCount> expected = {
        0x44, 0x33, 0x22, 0x11,
        0x88, 0x77, 0x66, 0x55,
        0xcc, 0xbb, 0xaa, 0x99,
        0x00, 0xff, 0xee, 0xdd,
    };

    EXPECT_EQ(bytes, expected);
}

TEST(MatPacking, DeserializeWordRowLittleEndian)
{
    MatRegContainer reg;
    const std::array<uint8_t, MatRowByteCount> bytes = {
        0x10, 0x32, 0x54, 0x76,
        0x98, 0xba, 0xdc, 0xfe,
        0xef, 0xcd, 0xab, 0x89,
        0x67, 0x45, 0x23, 0x01,
    };

    deserializeMatWordRow(bytes, reg, 1);

    auto row = getHSlice<uint32_t>(reg, 1);
    EXPECT_EQ(row[0], 0x76543210u);
    EXPECT_EQ(row[1], 0xfedcba98u);
    EXPECT_EQ(row[2], 0x89abcdefu);
    EXPECT_EQ(row[3], 0x01234567u);
}

} // namespace RiscvISA
} // namespace gem5
