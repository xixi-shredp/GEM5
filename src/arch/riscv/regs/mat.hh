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

#include "arch/arm/matrix.hh"
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

} // namespace RiscvISA
} // namespace gem5

#endif // __ARCH_RISCV_REGS_MAT_HH__
