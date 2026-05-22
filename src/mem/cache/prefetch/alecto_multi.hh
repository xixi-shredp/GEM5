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

#ifndef __MEM_CACHE_PREFETCH_ALECTO_MULTI_HH__
#define __MEM_CACHE_PREFETCH_ALECTO_MULTI_HH__

#include <cstdint>
#include <vector>

#include "base/statistics.hh"
#include "mem/cache/prefetch/base.hh"

namespace gem5
{

struct AlectoMultiPrefetchersParams;

namespace prefetch
{

class Queued;

class AlectoMulti : public Base
{
  public:
    AlectoMulti(const AlectoMultiPrefetchersParams &p);

    void setParentInfo(System *sys, ProbeManager *pm,
                       unsigned blk_size) override;
    PacketPtr getPacket() override;
    Tick nextPrefetchReadyTime() const override;
    void prefetchUnused() override;
    void incrDemandMhsrMisses() override;

    void notify(const CacheAccessProbeArg &arg,
                const PrefetchInfo &pfi) override;
    void notifyFill(const CacheAccessProbeArg &arg) override;
    void notifyEvict(const CacheDataUpdateProbeArg &info) override;
    void notifyPrefetchAccepted(PacketPtr pkt) override;
    void notifyPrefetchDropped(PacketPtr pkt) override;

  private:
    enum class StateKind : uint8_t
    {
        UI,
        IA,
        IB
    };

    struct PrefetcherState
    {
        StateKind kind = StateKind::UI;
        int level = 0;
    };

    struct AllocationEntry
    {
        bool valid = false;
        Addr tag = 0;
        std::vector<PrefetcherState> states;
    };

    struct SampleEntry
    {
        bool valid = false;
        Addr tag = 0;
        std::vector<uint32_t> issued;
        std::vector<uint32_t> confirmed;
        uint32_t demands = 0;
        uint32_t dead = 0;
    };

    struct SandboxEntry
    {
        bool valid = false;
        Addr tag = 0;
        bool secure = false;
        std::vector<bool> childValid;
        std::vector<Addr> childPc;
    };

    struct PendingIssuedPrefetch
    {
        PacketPtr pkt = nullptr;
        Addr pc = 0;
        unsigned pfIndex = 0;
        Addr blkAddr = 0;
        bool secure = false;
    };

    struct AlectoStats : public statistics::Group
    {
        AlectoStats(statistics::Group *parent);

        statistics::Scalar demandLookups;
        statistics::Scalar selectedChildren;
        statistics::Scalar queuedCandidates;
        statistics::Scalar duplicateFiltered;
        statistics::Scalar sandboxConfirmations;
        statistics::Scalar stateUpdates;
        statistics::Scalar deadResets;
    } alectoStats;

    std::vector<Base *> prefetchers;
    std::vector<AllocationEntry> allocationTable;
    std::vector<SampleEntry> sampleTable;
    std::vector<SandboxEntry> sandboxTable;
    std::vector<PendingIssuedPrefetch> pendingIssuedPrefetches;
    std::vector<unsigned> temporalPrefetcherIndices;
    uint8_t lastChosenPf;
    const unsigned numPrefetchers;
    const unsigned allocationEntries;
    const unsigned sampleEntries;
    const unsigned sandboxEntries;
    const unsigned epochAccesses;
    const unsigned conservativeDegree;
    const unsigned maxAggressiveLevel;
    const unsigned proficiencyThresholdPct;
    const unsigned deficiencyThresholdPct;
    const unsigned blockedEpochs;
    const unsigned deadCounterThreshold;

    size_t pcIndex(Addr pc, unsigned entries) const;
    size_t addrIndex(Addr addr, bool secure) const;
    bool isTemporal(unsigned pf_index) const;
    bool hasAggressiveState(const AllocationEntry &entry) const;
    bool allChildrenBlocked(const AllocationEntry &entry) const;
    bool isQueuedByAnyChild(const PrefetchInfo &pfi) const;
    Base *findChildByRequestorId(RequestorID requestor) const;

    AllocationEntry &allocationEntry(Addr pc);
    SampleEntry &sampleEntry(Addr pc);
    SandboxEntry *findSandboxEntry(Addr blk_addr, bool secure);
    SandboxEntry &sandboxEntryForInsert(Addr blk_addr, bool secure);

    void resetAllocationEntry(AllocationEntry &entry, Addr pc);
    void resetSampleEntry(SampleEntry &entry, Addr pc, bool keep_dead);
    void resetSandboxEntry(SandboxEntry &entry, Addr blk_addr, bool secure);
    void resetStatesToUI(AllocationEntry &entry);

    unsigned degreeForState(const PrefetcherState &state) const;
    void updateConfirmations(Addr pc, Addr blk_addr, bool secure);
    void recordIssuedPrefetch(Addr pc, unsigned pf_index, Addr blk_addr,
                              bool secure);
    void updateDeadCounter(AllocationEntry &alloc, SampleEntry &sample,
                           bool generated_prefetch);
    void updateAllocationStates(AllocationEntry &alloc, SampleEntry &sample);
};

} // namespace prefetch
} // namespace gem5

#endif // __MEM_CACHE_PREFETCH_ALECTO_MULTI_HH__
