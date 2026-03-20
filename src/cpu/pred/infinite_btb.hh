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

#ifndef __CPU_PRED_INFINITE_BTB_HH__
#define __CPU_PRED_INFINITE_BTB_HH__

#include <memory>
#include <unordered_map>

#include "base/types.hh"
#include "cpu/pred/btb.hh"
#include "cpu/static_inst.hh"
#include "params/InfiniteBTB.hh"

namespace gem5::branch_prediction
{

class InfiniteBTB : public BranchTargetBuffer
{
  public:
    InfiniteBTB(const InfiniteBTBParams &params);

    void memInvalidate() override;
    bool valid(ThreadID tid, Addr instPC) override;
    const PCStateBase *lookup(ThreadID tid, Addr instPC,
                              BranchType type = BranchType::NoBranch) override;
    void update(ThreadID tid, Addr instPC, const PCStateBase &target_pc,
                BranchType type = BranchType::NoBranch,
                StaticInstPtr inst = nullptr) override;
    const StaticInstPtr getInst(ThreadID tid, Addr instPC) override;

  private:
    struct Key
    {
        Addr instPC;
        ThreadID tid;

        bool
        operator==(const Key &other) const
        {
            return instPC == other.instPC && tid == other.tid;
        }
    };

    struct KeyHash
    {
        std::size_t
        operator()(const Key &key) const
        {
            auto pc_hash = std::hash<Addr>{}(key.instPC);
            auto tid_hash = std::hash<ThreadID>{}(key.tid);
            return pc_hash ^ (tid_hash << 1);
        }
    };

    struct Entry
    {
        std::unique_ptr<PCStateBase> target;
        StaticInstPtr inst;
    };

    using Storage = std::unordered_map<Key, Entry, KeyHash>;

    Entry *findEntry(ThreadID tid, Addr instPC);
    const Entry *findEntry(ThreadID tid, Addr instPC) const;

    Storage btb;
};

} // namespace gem5::branch_prediction

#endif // __CPU_PRED_INFINITE_BTB_HH__
