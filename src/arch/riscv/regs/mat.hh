/*
 * Copyright (c) 2026
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __ARCH_RISCV_REGS_MAT_HH__
#define __ARCH_RISCV_REGS_MAT_HH__

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "arch/arm/matrix.hh"
#include "cpu/exec_context.hh"
#include "cpu/o3/dyn_inst.hh"
#include "cpu/reg_class.hh"
#include "debug/MatRegs.hh"

namespace gem5
{

namespace RiscvISA
{

using MatRegContainer = gem5::MatStore<16, 4>;

template <typename ElemType>
using MatTile = gem5::Tile<ElemType, MatRegContainer>;

template <typename ElemType>
using MatRow = gem5::HorizontalSlice<ElemType, MatRegContainer, false>;

template <typename ElemType>
using MatCol = gem5::VerticalSlice<ElemType, MatRegContainer, false>;

const int NumMatRegs = 8;
inline constexpr std::size_t MatRowCount = 4;
inline constexpr std::size_t MatWordCountPerRow = 4;
inline constexpr std::size_t MatRowByteCount = 16;

using MatRowBytes = std::array<uint8_t, MatRowByteCount>;

struct MatLoadState
{
    MatRegContainer tile = {};
    std::array<bool, MatRowCount> valid = {};

    void
    reset()
    {
        tile = MatRegContainer();
        valid.fill(false);
    }
};

inline TypedRegClassOps<RiscvISA::MatRegContainer> matRegClassOps;

inline constexpr RegClass matRegClass =
    RegClass(MatRegClass, MatRegClassName, NumMatRegs, debug::MatRegs).
        ops(matRegClassOps).
        regType<MatRegContainer>();

template <typename ElemType>
MatTile<ElemType>
getTile(MatRegContainer &reg, uint8_t tile_idx = 0)
{
    return reg.asTile<ElemType>(tile_idx);
}

template <typename ElemType>
MatRow<ElemType>
getHSlice(MatRegContainer &reg, uint8_t row_idx)
{
    return reg.asHSlice<ElemType>(row_idx);
}

template <typename ElemType>
MatCol<ElemType>
getVSlice(MatRegContainer &reg, uint8_t col_idx)
{
    return reg.asVSlice<ElemType>(col_idx);
}

inline void
serializeMatRowBytes(const MatRegContainer &reg, uint8_t row_idx,
                     MatRowBytes &bytes)
{
    auto src = getHSlice<uint32_t>(const_cast<MatRegContainer &>(reg), row_idx);
    for (std::size_t word = 0; word < MatWordCountPerRow; ++word) {
        const auto value = src[word];
        const auto byte_base = word * sizeof(uint32_t);
        bytes[byte_base + 0] = bits(value, 7, 0);
        bytes[byte_base + 1] = bits(value, 15, 8);
        bytes[byte_base + 2] = bits(value, 23, 16);
        bytes[byte_base + 3] = bits(value, 31, 24);
    }
}

inline MatRowBytes
serializeMatRowBytes(const MatRegContainer &reg, uint8_t row_idx)
{
    MatRowBytes bytes = {};
    serializeMatRowBytes(reg, row_idx, bytes);
    return bytes;
}

inline void
deserializeMatWordRow(const MatRowBytes &bytes, MatRegContainer &reg,
                      uint8_t row_idx)
{
    auto dst = getHSlice<uint32_t>(reg, row_idx);
    for (std::size_t word = 0; word < MatWordCountPerRow; ++word) {
        const auto byte_base = word * sizeof(uint32_t);
        dst[word] =
            static_cast<uint32_t>(bytes[byte_base + 0]) |
            (static_cast<uint32_t>(bytes[byte_base + 1]) << 8) |
            (static_cast<uint32_t>(bytes[byte_base + 2]) << 16) |
            (static_cast<uint32_t>(bytes[byte_base + 3]) << 24);
    }
}

inline MatLoadState &
getOrCreateMatLoadState(ExecContext *xc,
                        std::shared_ptr<MatLoadState> &fallback)
{
    if (auto *dyn = dynamic_cast<o3::DynInst *>(xc)) {
        if (dyn->macroDynState) {
            if (!dyn->macroDynState->auxData) {
                dyn->macroDynState->auxData =
                    std::make_shared<MatLoadState>();
            }
            return *std::static_pointer_cast<MatLoadState>(
                dyn->macroDynState->auxData);
        }
    }

    if (!fallback) {
        fallback = std::make_shared<MatLoadState>();
    }
    return *fallback;
}

} // namespace RiscvISA
} // namespace gem5

#endif // __ARCH_RISCV_REGS_MAT_HH__
