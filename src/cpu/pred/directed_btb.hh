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

#ifndef __CPU_PRED_DIRECTED_BTB_HH__
#define __CPU_PRED_DIRECTED_BTB_HH__

#include "base/cache/associative_cache.hh"
#include "base/sat_counter.hh"
#include "cpu/pred/btb.hh"
#include "cpu/pred/btb_entry.hh"
#include "mem/cache/replacement_policies/base.hh"
#include "params/DirectedBTB.hh"

namespace gem5::branch_prediction
{

class DirectedBTBEntry : public BTBEntry
{
  public:
    DirectedBTBEntry(TagExtractor ext, unsigned counter_bits = 2,
                     unsigned initial_counter = 0);
    DirectedBTBEntry(const DirectedBTBEntry &other);
    DirectedBTBEntry &operator=(const DirectedBTBEntry &other);

    void resetDirectionState(bool is_conditional, unsigned initial_counter);
    void updateDirection(bool taken);
    bool directsTaken(unsigned threshold) const;
    bool replacementCandidate(unsigned threshold) const;

  private:
    unsigned counterBits;
    SatCounter8 takenCounter;
    bool conditional;
};

class DirectedBTB : public BranchTargetBuffer
{
  public:
    typedef DirectedBTBParams Params;

    explicit DirectedBTB(const Params &params);

    void memInvalidate() override;
    bool valid(ThreadID tid, Addr instPC) override;
    const PCStateBase *lookup(ThreadID tid, Addr instPC,
                              BranchType type = BranchType::NoBranch) override;
    const StaticInstPtr getInst(ThreadID tid, Addr instPC) override;
    bool lookupDirectionHint(ThreadID tid, Addr instPC,
                             BranchType type) override;
    void update(ThreadID tid, Addr instPC, const PCStateBase &target_pc,
                BranchType type = BranchType::NoBranch,
                StaticInstPtr inst = nullptr) override;
    void updateDirectionInfo(ThreadID tid, Addr inst_pc, BranchType type,
                             bool actually_taken) override;

  private:
    static bool isConditionalBranch(BranchType type);

    DirectedBTBEntry *findEntry(Addr instPC, ThreadID tid);
    DirectedBTBEntry *findVictim(Addr instPC, ThreadID tid);

    AssociativeCache<DirectedBTBEntry> btb;
    replacement_policy::Base *replPolicy;
    unsigned initialTakenCounter;
    unsigned directedTakenThreshold;
    unsigned replacementCandidateThreshold;
};

} // namespace gem5::branch_prediction

#endif // __CPU_PRED_DIRECTED_BTB_HH__
