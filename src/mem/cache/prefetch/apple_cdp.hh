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

#ifndef __MEM_CACHE_PREFETCH_APPLE_CDP_HH__
#define __MEM_CACHE_PREFETCH_APPLE_CDP_HH__

#include <cstdint>
#include <list>
#include <unordered_map>
#include <vector>

#include "base/sat_counter.hh"
#include "base/statistics.hh"
#include "base/types.hh"
#include "enums/ByteOrder.hh"
#include "mem/cache/prefetch/queued.hh"
#include "mem/packet.hh"
#include "mem/request.hh"

namespace gem5
{

struct AppleCDPPrefetcherParams;

namespace prefetch
{

class AppleCDP : public Queued
{
  private:
    struct QFIndex
    {
        uint32_t pc = 0;
        uint32_t relativeOffset = 0;

        bool
        operator==(const QFIndex &other) const
        {
            return pc == other.pc && relativeOffset == other.relativeOffset;
        }
    };

    struct HistoryEntry
    {
        bool valid = false;
        Addr tag = 0;
    };

    struct PendingPrefetch
    {
        QFIndex qfIndex;
        Addr triggerAddr = 0;
        Request::Flags flags;
        RequestorID requestorId = 0;
        bool hasContextId = false;
        ContextID contextId = InvalidContextID;
        bool hasPC = false;
        Addr pc = 0;
        bool adjacentLine = false;
        bool issueAdjacentOnIssue = false;
        Addr adjacentAddr = 0;

        bool
        operator==(const PendingPrefetch &other) const
        {
            return qfIndex == other.qfIndex &&
                   triggerAddr == other.triggerAddr &&
                   requestorId == other.requestorId &&
                   hasContextId == other.hasContextId &&
                   contextId == other.contextId && hasPC == other.hasPC &&
                   pc == other.pc && adjacentLine == other.adjacentLine &&
                   issueAdjacentOnIssue == other.issueAdjacentOnIssue &&
                   adjacentAddr == other.adjacentAddr;
        }
    };

    struct DeferredPacket : public BaseMMU::Translation
    {
        AppleCDP *owner;
        PrefetchInfo pfInfo;
        PendingPrefetch meta;
        Tick tick = 0;
        PacketPtr pkt = nullptr;
        int32_t priority = 0;
        RequestPtr translationRequest;
        ThreadContext *tc = nullptr;
        bool ongoingTranslation = false;
        bool demandSquashed = false;
        const CacheAccessor *cache = nullptr;

        DeferredPacket(AppleCDP *owner, const PrefetchInfo &pfi,
                       const PendingPrefetch &meta, int32_t priority,
                       const CacheAccessor &cache);

        bool
        operator>(const DeferredPacket &that) const
        {
            return priority > that.priority;
        }
        bool
        operator<=(const DeferredPacket &that) const
        {
            return !(*this > that);
        }

        void createPkt(Addr paddr, unsigned blk_size, RequestorID requestor_id,
                       bool tag_prefetch, Tick t);
        void
        setTranslationRequest(const RequestPtr &req)
        {
            translationRequest = req;
        }
        void startTranslation(BaseMMU *mmu);
        void
        markDelayed() override
        {}
        void finish(const Fault &fault, const RequestPtr &req,
                    ThreadContext *tc, BaseMMU::Mode mode) override;
    };

    struct PrefetchRequestEntry
    {
        bool valid = false;
        Addr tag = 0;
        QFIndex qfIndex;
        Addr triggerAddr = 0;
        Addr triggerPaddr = 0;
        Request::Flags flags;
        RequestorID requestorId = 0;
        bool hasContextId = false;
        ContextID contextId = InvalidContextID;
        bool hasPC = false;
        Addr pc = 0;
        bool adjacentLine = false;
    };

    struct AppleCDPStats : public statistics::Group
    {
        AppleCDPStats(statistics::Group *parent);

        statistics::Scalar fillsScanned;
        statistics::Scalar pointerCandidates;
        statistics::Scalar historyFiltered;
        statistics::Scalar qfFiltered;
        statistics::Scalar globalFiltered;
        statistics::Scalar mainPrefetches;
        statistics::Scalar adjacentPrefetches;
        statistics::Scalar requestCacheHits;
        statistics::Scalar requestCacheMisses;
        statistics::Scalar qfResets;
    } statsAppleCDP;

    const unsigned pointerBytes;
    const unsigned scanGranularity;
    const unsigned pointerMatchBits;
    const Addr minCandidateAddr;
    const Addr maxCandidateAddr;
    const bool rejectNegativePointers;
    const bool rejectZeroHighBits;
    const bool prefetchSameLine;
    const bool scanPrefetchFills;
    const bool requirePC;
    const bool enableAdjacentLine;
    const int adjacentLineDistance;

    const unsigned qfPcEntries;
    const unsigned qfRelativeOffsetEntries;
    const unsigned qfCounterThreshold;
    const unsigned adjacentLineThreshold;
    const unsigned globalThreshold;
    const unsigned qfResetInterval;

    std::vector<SatCounter8> qfCounters;
    std::vector<SatCounter8> adjacentLineCounters;
    SatCounter8 globalQfCounter;

    std::vector<HistoryEntry> historyFilter;
    std::vector<PrefetchRequestEntry> prefetchRequestCache;
    std::unordered_map<Addr, PendingPrefetch> pendingPrefetches;
    std::list<DeferredPacket> readyQueue;
    std::list<DeferredPacket> translationQueue;
    uint64_t scannedFillCount;

    const ByteOrder byteOrder;

    static uint8_t checkedCounterInitial(unsigned bits, unsigned initial);
    static unsigned computeRelativeOffsetEntries(unsigned block_size,
                                                 unsigned granularity);

    uint64_t hashAddress(Addr addr) const;
    unsigned pcIndex(Addr pc) const;
    unsigned relativeOffset(Addr trigger_addr,
                            unsigned candidate_offset) const;
    size_t qfFlatIndex(const QFIndex &idx) const;

    bool detectPointerCandidate(const uint8_t *line, unsigned line_size,
                                unsigned offset, Addr trigger_addr,
                                Addr &candidate) const;

    bool historyFilterHit(Addr candidate) const;
    void insertHistoryFilter(Addr candidate);
    bool hasPendingTranslation(Addr addr) const;

    void resetQfCounters();
    void debitGlobalCounter();
    void creditPrefetch(const QFIndex &idx, bool adjacent_line);
    void chargeQueuedPrefetch(const PrefetchInfo &pfi,
                              const PendingPrefetch &meta);

    RequestPtr createPrefetchRequest(Addr addr, const PrefetchInfo &pfi,
                                     PacketPtr pkt) const;
    bool alreadyInQueue(std::list<DeferredPacket> &queue,
                        const PrefetchInfo &pfi, int32_t priority);
    bool addToQueue(std::list<DeferredPacket> &queue, DeferredPacket &dpp);
    void processMissingTranslations(unsigned max);
    void translationComplete(DeferredPacket *dp, bool failed);
    bool insertPrefetch(const PacketPtr &pkt, PrefetchInfo &new_pfi,
                        const PendingPrefetch &meta, int32_t priority,
                        const CacheAccessor &cache);
    bool queuePrefetch(const PacketPtr &pkt, const PrefetchInfo &source_pfi,
                       Addr addr, const QFIndex &idx, bool adjacent_line,
                       const CacheAccessor &cache, bool issue_adjacent = false,
                       Addr adjacent_addr = 0);
    void rememberPrefetchRequest(Addr paddr, const PendingPrefetch &meta);
    bool processPrefetchFill(const PacketPtr &pkt, bool from_this_prefetcher,
                             PrefetchRequestEntry &matched_entry);
    bool prefetchIssueAllowed(const PrefetchInfo &pfi, Addr paddr,
                              int32_t priority, const CacheAccessor &cache);
    void prefetchIssued(const PrefetchInfo &pfi, Addr paddr, int32_t priority,
                        const CacheAccessor &cache);
    void prefetchDropped(const PrefetchInfo &pfi, Addr paddr,
                         int32_t priority);

  public:
    AppleCDP(const AppleCDPPrefetcherParams &p);
    ~AppleCDP() override;

    void notify(const CacheAccessProbeArg &arg,
                const PrefetchInfo &pfi) override;
    void notifyFill(const CacheAccessProbeArg &arg) override;
    PacketPtr getPacket() override;
    Tick
    nextPrefetchReadyTime() const override
    {
        return readyQueue.empty() ? MaxTick : readyQueue.front().tick;
    }

    void
    calculatePrefetch(const PrefetchInfo &pfi,
                      std::vector<AddrPriority> &addresses,
                      const CacheAccessor &cache) override
    {}
};

} // namespace prefetch
} // namespace gem5

#endif // __MEM_CACHE_PREFETCH_APPLE_CDP_HH__
