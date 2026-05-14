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

#include "mem/cache/prefetch/apple_cdp.hh"

#include <algorithm>
#include <cstring>
#include <limits>
#include <memory>

#include "base/intmath.hh"
#include "base/logging.hh"
#include "debug/HWPrefetch.hh"
#include "params/AppleCDPPrefetcher.hh"
#include "sim/byteswap.hh"
#include "sim/system.hh"

namespace gem5
{

namespace prefetch
{

uint8_t
AppleCDP::checkedCounterInitial(unsigned bits, unsigned initial)
{
    fatal_if(bits == 0 || bits > 8,
             "AppleCDP QF counters must use between 1 and 8 bits");
    const unsigned max_value = (1u << bits) - 1;
    fatal_if(initial > max_value,
             "AppleCDP QF initial value %u exceeds %u-bit maximum %u", initial,
             bits, max_value);
    return initial;
}

unsigned
AppleCDP::computeRelativeOffsetEntries(unsigned block_size,
                                       unsigned granularity)
{
    fatal_if(granularity == 0,
             "AppleCDP scan granularity must be greater than zero");
    fatal_if(block_size == 0 || block_size % granularity != 0,
             "AppleCDP block size must be a non-zero multiple of scan "
             "granularity");
    const unsigned slots = block_size / granularity;
    fatal_if(slots == 0, "AppleCDP must have at least one scan slot");
    return 2 * slots - 1;
}

AppleCDP::AppleCDP(const AppleCDPPrefetcherParams &p)
    : Queued(p),
      statsAppleCDP(this),
      pointerBytes(p.pointer_bytes),
      scanGranularity(p.scan_granularity),
      pointerMatchBits(p.pointer_match_bits),
      minCandidateAddr(p.min_candidate_address),
      maxCandidateAddr(p.max_candidate_address),
      rejectNegativePointers(p.reject_negative_pointers),
      rejectZeroHighBits(p.reject_zero_high_bits),
      prefetchSameLine(p.prefetch_same_line),
      scanPrefetchFills(p.scan_prefetch_fills),
      requirePC(p.require_pc),
      enableAdjacentLine(p.enable_adjacent_line),
      adjacentLineDistance(p.adjacent_line_distance),
      qfPcEntries(p.qf_pc_entries),
      qfRelativeOffsetEntries(
          computeRelativeOffsetEntries(p.block_size, p.scan_granularity)),
      qfCounterThreshold(p.qf_counter_threshold),
      adjacentLineThreshold(p.adjacent_line_threshold),
      globalThreshold(p.global_threshold),
      qfResetInterval(p.qf_reset_interval),
      qfCounters(qfPcEntries * qfRelativeOffsetEntries,
                 SatCounter8(p.qf_counter_bits,
                             checkedCounterInitial(p.qf_counter_bits,
                                                   p.qf_initial_value))),
      adjacentLineCounters(
          qfPcEntries, SatCounter8(p.qf_counter_bits,
                                   checkedCounterInitial(p.qf_counter_bits,
                                                         p.qf_initial_value))),
      globalQfCounter(
          p.qf_counter_bits,
          checkedCounterInitial(p.qf_counter_bits, p.global_initial_value)),
      historyFilter(p.history_entries),
      prefetchRequestCache(p.request_cache_entries),
      scannedFillCount(0),
      byteOrder(p.sys->getGuestByteOrder())
{
    fatal_if(pointerBytes != 4 && pointerBytes != 8,
             "AppleCDP pointer_bytes must be either 4 or 8");
    fatal_if(scanGranularity < pointerBytes,
             "AppleCDP scan granularity must be at least pointer_bytes");
    fatal_if(pointerMatchBits > pointerBytes * 8,
             "AppleCDP pointer_match_bits exceeds pointer width");
    fatal_if(qfPcEntries == 0,
             "AppleCDP QF table must have at least one PC entry");
    fatal_if(historyFilter.empty(),
             "AppleCDP history filter must have at least one entry");
    fatal_if(prefetchRequestCache.empty(),
             "AppleCDP prefetch request cache must have at least one entry");
    fatal_if(adjacentLineDistance == 0,
             "AppleCDP adjacent line distance must be non-zero");
}

AppleCDP::~AppleCDP()
{
    for (DeferredPacket &dp : readyQueue) {
        delete dp.pkt;
    }
}

AppleCDP::DeferredPacket::DeferredPacket(AppleCDP *owner,
                                         const PrefetchInfo &pfi,
                                         const PendingPrefetch &meta,
                                         int32_t priority,
                                         const CacheAccessor &cache)
    : owner(owner), pfInfo(pfi), meta(meta), priority(priority), cache(&cache)
{}

void
AppleCDP::DeferredPacket::createPkt(Addr paddr, unsigned blk_size,
                                    RequestorID requestor_id,
                                    bool tag_prefetch, Tick t)
{
    RequestPtr req =
        std::make_shared<Request>(paddr, blk_size, 0, requestor_id);
    if (pfInfo.isSecure()) {
        req->setFlags(Request::SECURE);
    }
    req->taskId(context_switch_task_id::Prefetcher);
    pkt = new Packet(req, MemCmd::HardPFReq);
    pkt->allocate();
    if (tag_prefetch && pfInfo.hasPC()) {
        pkt->req->setPC(pfInfo.getPC());
    }
    tick = t;
}

void
AppleCDP::DeferredPacket::startTranslation(BaseMMU *mmu)
{
    assert(translationRequest != nullptr);
    if (!ongoingTranslation) {
        ongoingTranslation = true;
        mmu->translateTiming(translationRequest, tc, this, BaseMMU::Read);
    }
}

void
AppleCDP::DeferredPacket::finish(const Fault &fault, const RequestPtr &req,
                                 ThreadContext *tc, BaseMMU::Mode mode)
{
    (void)req;
    (void)tc;
    (void)mode;
    assert(ongoingTranslation);
    ongoingTranslation = false;
    owner->translationComplete(this, fault != NoFault);
}

AppleCDP::AppleCDPStats::AppleCDPStats(statistics::Group *parent)
    : statistics::Group(parent),
      ADD_STAT(fillsScanned, statistics::units::Count::get(),
               "number of cache line fills scanned by AppleCDP"),
      ADD_STAT(pointerCandidates, statistics::units::Count::get(),
               "number of memory pointer candidates detected by AppleCDP"),
      ADD_STAT(historyFiltered, statistics::units::Count::get(),
               "number of AppleCDP candidates removed by the history filter"),
      ADD_STAT(qfFiltered, statistics::units::Count::get(),
               "number of AppleCDP candidates removed by the QF table"),
      ADD_STAT(
          globalFiltered, statistics::units::Count::get(),
          "number of AppleCDP candidates removed by the global QF counter"),
      ADD_STAT(mainPrefetches, statistics::units::Count::get(),
               "number of AppleCDP pointer-target prefetches issued"),
      ADD_STAT(adjacentPrefetches, statistics::units::Count::get(),
               "number of AppleCDP adjacent-line prefetches issued"),
      ADD_STAT(requestCacheHits, statistics::units::Count::get(),
               "number of AppleCDP prefetch fills that hit the request cache"),
      ADD_STAT(
          requestCacheMisses, statistics::units::Count::get(),
          "number of AppleCDP prefetch fills that missed the request cache"),
      ADD_STAT(qfResets, statistics::units::Count::get(),
               "number of periodic AppleCDP QF counter resets")
{}

uint64_t
AppleCDP::hashAddress(Addr addr) const
{
    uint64_t x = addr;
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return x;
}

unsigned
AppleCDP::pcIndex(Addr pc) const
{
    return hashAddress(pc) % qfPcEntries;
}

unsigned
AppleCDP::relativeOffset(Addr trigger_addr, unsigned candidate_offset) const
{
    const unsigned slots = blkSize / scanGranularity;
    const unsigned trigger_slot =
        (trigger_addr & (blkSize - 1)) / scanGranularity;
    const int raw_offset =
        static_cast<int>(candidate_offset) - static_cast<int>(trigger_slot);
    return raw_offset + slots - 1;
}

size_t
AppleCDP::qfFlatIndex(const QFIndex &idx) const
{
    return idx.pc * qfRelativeOffsetEntries + idx.relativeOffset;
}

bool
AppleCDP::detectPointerCandidate(const uint8_t *line, unsigned line_size,
                                 unsigned offset, Addr trigger_addr,
                                 Addr &candidate) const
{
    if (offset + pointerBytes > line_size) {
        return false;
    }

    if (pointerBytes == sizeof(uint64_t)) {
        uint64_t raw = 0;
        std::memcpy(&raw, line + offset, sizeof(raw));
        candidate = gtoh(raw, byteOrder);
    } else {
        uint32_t raw = 0;
        std::memcpy(&raw, line + offset, sizeof(raw));
        candidate = gtoh(raw, byteOrder);
    }

    if (candidate < minCandidateAddr) {
        return false;
    }
    if (maxCandidateAddr != 0 && candidate > maxCandidateAddr) {
        return false;
    }

    const unsigned width = pointerBytes * 8;
    const Addr width_mask = width == 64 ? std::numeric_limits<Addr>::max()
                                        : ((Addr(1) << width) - 1);
    const Addr candidate_value = candidate & width_mask;
    const Addr trigger_value = trigger_addr & width_mask;

    if (pointerMatchBits != 0) {
        const Addr low_mask =
            pointerMatchBits == width
                ? 0
                : ((Addr(1) << (width - pointerMatchBits)) - 1);
        const Addr high_mask = width_mask & ~low_mask;
        if ((candidate_value & high_mask) != (trigger_value & high_mask)) {
            return false;
        }
        if (rejectNegativePointers &&
            (candidate_value & high_mask) == high_mask) {
            return false;
        }
        if (rejectZeroHighBits && (candidate_value & high_mask) == 0) {
            return false;
        }
    }

    if (!prefetchSameLine &&
        blockAddress(candidate) == blockAddress(trigger_addr)) {
        return false;
    }

    return true;
}

bool
AppleCDP::historyFilterHit(Addr candidate) const
{
    const uint64_t hash = hashAddress(blockAddress(candidate));
    const size_t index = hash % historyFilter.size();
    const Addr tag = hash / historyFilter.size();
    return historyFilter[index].valid && historyFilter[index].tag == tag;
}

void
AppleCDP::insertHistoryFilter(Addr candidate)
{
    const uint64_t hash = hashAddress(blockAddress(candidate));
    const size_t index = hash % historyFilter.size();
    historyFilter[index].valid = true;
    historyFilter[index].tag = hash / historyFilter.size();
}

bool
AppleCDP::hasPendingTranslation(Addr addr) const
{
    const Addr blk_addr = blockAddress(addr);
    for (const auto &entry : translationQueue) {
        if (blockAddress(entry.pfInfo.getAddr()) == blk_addr) {
            return true;
        }
    }
    return false;
}

void
AppleCDP::resetQfCounters()
{
    for (auto &counter : qfCounters) {
        counter.reset();
    }
    for (auto &counter : adjacentLineCounters) {
        counter.reset();
    }
    globalQfCounter.reset();
    statsAppleCDP.qfResets++;
}

void
AppleCDP::debitGlobalCounter()
{
    --globalQfCounter;
}

void
AppleCDP::creditPrefetch(const QFIndex &idx, bool adjacent_line)
{
    if (adjacent_line) {
        ++adjacentLineCounters[idx.pc];
    } else {
        ++qfCounters[qfFlatIndex(idx)];
    }
    globalQfCounter.reset();
}

void
AppleCDP::chargeQueuedPrefetch(const PrefetchInfo &pfi,
                               const PendingPrefetch &meta)
{
    if (meta.adjacentLine) {
        --adjacentLineCounters[meta.qfIndex.pc];
        statsAppleCDP.adjacentPrefetches++;
    } else {
        insertHistoryFilter(pfi.getAddr());
        --qfCounters[qfFlatIndex(meta.qfIndex)];
        statsAppleCDP.mainPrefetches++;
    }
    debitGlobalCounter();
}

RequestPtr
AppleCDP::createPrefetchRequest(Addr addr, const PrefetchInfo &pfi,
                                PacketPtr pkt) const
{
    const Addr pc = pfi.hasPC() ? pfi.getPC() : 0;
    RequestPtr translation_req =
        std::make_shared<Request>(addr, blkSize, pkt->req->getFlags(),
                                  requestorId, pc, pkt->req->contextId());
    translation_req->setFlags(Request::PREFETCH);
    return translation_req;
}

bool
AppleCDP::alreadyInQueue(std::list<DeferredPacket> &queue,
                         const PrefetchInfo &pfi, int32_t priority)
{
    auto it = queue.begin();
    for (; it != queue.end(); ++it) {
        if (it->pfInfo.sameAddr(pfi)) {
            break;
        }
    }

    if (it == queue.end()) {
        return false;
    }

    statsQueued.pfBufferHit++;
    if (it->priority < priority) {
        it->priority = priority;
        auto prev = it;
        while (prev != queue.begin()) {
            --prev;
            if (*it > *prev) {
                std::swap(*it, *prev);
                it = prev;
            } else {
                break;
            }
        }
        DPRINTF(HWPrefetch, "AppleCDP prefetch addr already queued, "
                            "priority updated\n");
    } else {
        DPRINTF(HWPrefetch, "AppleCDP prefetch addr already queued\n");
    }
    return true;
}

bool
AppleCDP::addToQueue(std::list<DeferredPacket> &queue, DeferredPacket &dpp)
{
    if (queue.size() == queueSize) {
        statsQueued.pfRemovedFull++;

        panic_if(queue.empty(),
                 "AppleCDP prefetch queue is both full and empty!");

        auto it = queue.end();
        for (auto candidate = queue.begin(); candidate != queue.end();
             ++candidate) {
            if (candidate->ongoingTranslation) {
                continue;
            }
            if (it == queue.end() || candidate->priority < it->priority) {
                it = candidate;
            }
        }

        if (it == queue.end()) {
            DPRINTF(HWPrefetch,
                    "AppleCDP prefetch queue full with only "
                    "ongoing translations, dropping new addr:%#x\n",
                    dpp.pfInfo.getAddr());
            return false;
        }

        DPRINTF(HWPrefetch,
                "AppleCDP prefetch queue full, removing "
                "addr:%#x\n",
                it->pfInfo.getAddr());
        prefetchDropped(it->pfInfo, it->pkt ? it->pkt->getAddr() : MaxAddr,
                        it->priority);
        delete it->pkt;
        queue.erase(it);
    }

    if (queue.empty() || dpp <= queue.back()) {
        queue.emplace_back(dpp);
    } else {
        auto it = queue.end();
        do {
            --it;
        } while (it != queue.begin() && dpp > *it);
        if (it == queue.begin() && dpp <= *it) {
            ++it;
        }
        queue.insert(it, dpp);
    }

    return true;
}

void
AppleCDP::processMissingTranslations(unsigned max)
{
    unsigned count = 0;
    auto it = translationQueue.begin();
    while (it != translationQueue.end() && count < max) {
        DeferredPacket &dp = *it;
        ++it;
        dp.startTranslation(mmu);
        count++;
    }
}

void
AppleCDP::translationComplete(DeferredPacket *dp, bool failed)
{
    auto it = translationQueue.begin();
    while (it != translationQueue.end()) {
        if (&(*it) == dp) {
            break;
        }
        ++it;
    }
    assert(it != translationQueue.end());

    const CacheAccessor &cache = *it->cache;
    if (it->demandSquashed) {
        DPRINTF(HWPrefetch,
                "Dropping demand-squashed AppleCDP translated "
                "prefetch addr:%#x\n",
                it->pfInfo.getAddr());
        translationQueue.erase(it);
        return;
    }

    if (!failed) {
        DPRINTF(HWPrefetch,
                "%s AppleCDP translation of vaddr %#x "
                "succeeded: paddr %#x\n",
                mmu->name(), it->translationRequest->getVaddr(),
                it->translationRequest->getPaddr());
        const Addr target_paddr = it->translationRequest->getPaddr();
        if (cacheSnoop &&
            (cache.inCache(target_paddr, it->pfInfo.isSecure()) ||
             cache.inMissQueue(target_paddr, it->pfInfo.isSecure()))) {
            statsQueued.pfInCache++;
            DPRINTF(HWPrefetch,
                    "Dropping redundant AppleCDP prefetch "
                    "addr:%#x\n",
                    target_paddr);
            prefetchDropped(it->pfInfo, target_paddr, it->priority);
        } else {
            const Tick pf_time = curTick() + clockPeriod() * latency;
            it->createPkt(target_paddr, blkSize, requestorId, tagPrefetch,
                          pf_time);
            if (!addToQueue(readyQueue, *it)) {
                prefetchDropped(it->pfInfo, target_paddr, it->priority);
                delete it->pkt;
            }
        }
    } else {
        DPRINTF(HWPrefetch,
                "%s AppleCDP translation of vaddr %#x failed, "
                "dropping prefetch request %#x\n",
                mmu->name(), it->translationRequest->getVaddr());
        prefetchDropped(it->pfInfo, MaxAddr, it->priority);
    }

    translationQueue.erase(it);
}

bool
AppleCDP::insertPrefetch(const PacketPtr &pkt, PrefetchInfo &new_pfi,
                         const PendingPrefetch &meta, int32_t priority,
                         const CacheAccessor &cache)
{
    if (queueFilter) {
        if (alreadyInQueue(readyQueue, new_pfi, priority)) {
            return false;
        }
        if (alreadyInQueue(translationQueue, new_pfi, priority)) {
            return false;
        }
    }

    const Addr orig_addr = (useVirtualAddresses && pkt->req->hasVaddr())
                               ? pkt->req->getVaddr()
                               : pkt->req->getPaddr();
    const bool positive_stride = new_pfi.getAddr() >= orig_addr;
    const Addr stride = positive_stride ? (new_pfi.getAddr() - orig_addr)
                                        : (orig_addr - new_pfi.getAddr());

    Addr target_paddr = 0;
    bool has_target_pa = false;
    RequestPtr translation_req = nullptr;
    if (samePage(orig_addr, new_pfi.getAddr())) {
        if (useVirtualAddresses) {
            target_paddr = positive_stride ? (pkt->req->getPaddr() + stride)
                                           : (pkt->req->getPaddr() - stride);
        } else {
            target_paddr = new_pfi.getAddr();
        }
        has_target_pa = true;
    } else {
        if (mmu == nullptr || !pkt->req->hasContextId()) {
            return false;
        }
        if (useVirtualAddresses) {
            translation_req =
                createPrefetchRequest(new_pfi.getAddr(), new_pfi, pkt);
        } else if (pkt->req->hasVaddr()) {
            const Addr target_vaddr = positive_stride
                                          ? (pkt->req->getVaddr() + stride)
                                          : (pkt->req->getVaddr() - stride);
            translation_req =
                createPrefetchRequest(target_vaddr, new_pfi, pkt);
        } else {
            return false;
        }
    }

    if (has_target_pa && cacheSnoop &&
        (cache.inCache(target_paddr, new_pfi.isSecure()) ||
         cache.inMissQueue(target_paddr, new_pfi.isSecure()))) {
        statsQueued.pfInCache++;
        DPRINTF(HWPrefetch,
                "Dropping redundant AppleCDP prefetch "
                "addr:%#x\n",
                target_paddr);
        return false;
    }

    DeferredPacket dpp(this, new_pfi, meta, priority, cache);
    if (has_target_pa) {
        const Tick pf_time = curTick() + clockPeriod() * latency;
        dpp.createPkt(target_paddr, blkSize, requestorId, tagPrefetch,
                      pf_time);
        DPRINTF(HWPrefetch,
                "AppleCDP prefetch queued addr:%#x "
                "priority:%3d tick:%lld.\n",
                new_pfi.getAddr(), priority, pf_time);
        if (!addToQueue(readyQueue, dpp)) {
            delete dpp.pkt;
            return false;
        }
    } else {
        dpp.setTranslationRequest(translation_req);
        dpp.tc = system->threads[translation_req->contextId()];
        DPRINTF(HWPrefetch,
                "AppleCDP prefetch queued with translation "
                "addr:%#x priority:%3d\n",
                new_pfi.getAddr(), priority);
        if (!addToQueue(translationQueue, dpp)) {
            return false;
        }
    }
    return true;
}

bool
AppleCDP::queuePrefetch(const PacketPtr &pkt, const PrefetchInfo &source_pfi,
                        Addr addr, const QFIndex &idx, bool adjacent_line,
                        const CacheAccessor &cache, bool issue_adjacent,
                        Addr adjacent_addr)
{
    PendingPrefetch meta;
    meta.qfIndex = idx;
    meta.triggerAddr = addr;
    meta.flags = pkt->req->getFlags();
    meta.requestorId = pkt->req->requestorId();
    meta.hasContextId = pkt->req->hasContextId();
    if (meta.hasContextId) {
        meta.contextId = pkt->req->contextId();
    }
    meta.hasPC = source_pfi.hasPC();
    if (meta.hasPC) {
        meta.pc = source_pfi.getPC();
    }
    meta.adjacentLine = adjacent_line;
    meta.issueAdjacentOnIssue = issue_adjacent;
    meta.adjacentAddr = adjacent_addr;
    const Addr prefetch_addr = blockAddress(addr);
    const bool preserve_pending =
        pendingPrefetches.find(prefetch_addr) != pendingPrefetches.end() ||
        hasPendingTranslation(prefetch_addr);
    if (!preserve_pending) {
        pendingPrefetches[prefetch_addr] = meta;
    }

    PrefetchInfo new_pfi(source_pfi, prefetch_addr);
    statsQueued.pfIdentified++;
    const bool accepted =
        insertPrefetch(pkt, new_pfi, meta, adjacent_line ? -1 : 0, cache);

    if (!accepted && !preserve_pending) {
        auto pending_it = pendingPrefetches.find(prefetch_addr);
        if (pending_it != pendingPrefetches.end() &&
            pending_it->second == meta) {
            pendingPrefetches.erase(pending_it);
        }
    }

    return accepted;
}

void
AppleCDP::rememberPrefetchRequest(Addr paddr, const PendingPrefetch &meta)
{
    const uint64_t hash = hashAddress(blockAddress(paddr));
    const size_t index = hash % prefetchRequestCache.size();

    PrefetchRequestEntry &entry = prefetchRequestCache[index];
    entry.valid = true;
    entry.tag = hash / prefetchRequestCache.size();
    entry.qfIndex = meta.qfIndex;
    entry.triggerAddr = meta.triggerAddr;
    entry.triggerPaddr =
        blockAddress(paddr) + (meta.triggerAddr & (blkSize - 1));
    entry.flags = meta.flags;
    entry.requestorId = meta.requestorId;
    entry.hasContextId = meta.hasContextId;
    entry.contextId = meta.contextId;
    entry.hasPC = meta.hasPC;
    entry.pc = meta.pc;
    entry.adjacentLine = meta.adjacentLine;
}

void
AppleCDP::notify(const CacheAccessProbeArg &arg, const PrefetchInfo &pfi)
{
    (void)arg;
    if (!queueSquash) {
        return;
    }

    const Addr blk_addr = blockAddress(pfi.getAddr());
    const bool is_secure = pfi.isSecure();

    auto squash_queue = [&](std::list<DeferredPacket> &queue) {
        auto it = queue.begin();
        while (it != queue.end()) {
            if (blockAddress(it->pfInfo.getAddr()) == blk_addr &&
                it->pfInfo.isSecure() == is_secure) {
                if (it->ongoingTranslation) {
                    if (!it->demandSquashed) {
                        DPRINTF(HWPrefetch,
                                "Marking AppleCDP candidate "
                                "addr:%#x (cl:%#x) for removal after "
                                "translation, demand request going to the "
                                "same addr\n",
                                it->pfInfo.getAddr(),
                                blockAddress(it->pfInfo.getAddr()));
                        it->demandSquashed = true;
                        prefetchDropped(it->pfInfo,
                                        it->pkt ? it->pkt->getAddr() : MaxAddr,
                                        it->priority);
                        statsQueued.pfRemovedDemand++;
                    }
                    ++it;
                    continue;
                }
                DPRINTF(HWPrefetch,
                        "Removing AppleCDP candidate addr:%#x "
                        "(cl:%#x), demand request going to the same addr\n",
                        it->pfInfo.getAddr(),
                        blockAddress(it->pfInfo.getAddr()));
                prefetchDropped(it->pfInfo,
                                it->pkt ? it->pkt->getAddr() : MaxAddr,
                                it->priority);
                delete it->pkt;
                it = queue.erase(it);
                statsQueued.pfRemovedDemand++;
            } else {
                ++it;
            }
        }
    };

    squash_queue(readyQueue);
    squash_queue(translationQueue);
}

PacketPtr
AppleCDP::getPacket()
{
    DPRINTF(HWPrefetch, "Requesting an AppleCDP prefetch to issue.\n");

    while (true) {
        if (readyQueue.empty()) {
            processMissingTranslations(queueSize);
        }

        if (readyQueue.empty()) {
            DPRINTF(HWPrefetch, "No AppleCDP prefetches available.\n");
            return nullptr;
        }

        DeferredPacket &dp = readyQueue.front();
        PacketPtr pkt = dp.pkt;
        assert(pkt != nullptr);
        const Addr paddr = pkt->getAddr();
        const int32_t priority = dp.priority;
        const CacheAccessor &cache = *dp.cache;
        PrefetchInfo issued_pfi(dp.pfInfo, dp.pfInfo.getAddr());

        if (!prefetchIssueAllowed(issued_pfi, paddr, priority, cache)) {
            prefetchDropped(issued_pfi, paddr, priority);
            delete pkt;
            readyQueue.pop_front();
            processMissingTranslations(queueSize - readyQueue.size());
            continue;
        }

        readyQueue.pop_front();
        prefetchIssued(issued_pfi, paddr, priority, cache);

        prefetchStats.pfIssued++;
        issuedPrefetches += 1;
        DPRINTF(HWPrefetch, "Generating AppleCDP prefetch for %#x.\n",
                pkt->getAddr());

        processMissingTranslations(queueSize - readyQueue.size());
        return pkt;
    }
}

bool
AppleCDP::prefetchIssueAllowed(const PrefetchInfo &pfi, Addr paddr,
                               int32_t priority, const CacheAccessor &cache)
{
    (void)paddr;
    (void)priority;
    (void)cache;

    auto pending_it = pendingPrefetches.find(blockAddress(pfi.getAddr()));
    if (pending_it == pendingPrefetches.end()) {
        return false;
    }

    const PendingPrefetch &meta = pending_it->second;
    if (globalQfCounter < globalThreshold) {
        statsAppleCDP.globalFiltered++;
        return false;
    }

    if (meta.adjacentLine) {
        if (adjacentLineCounters[meta.qfIndex.pc] < adjacentLineThreshold) {
            return false;
        }
    } else if (qfCounters[qfFlatIndex(meta.qfIndex)] < qfCounterThreshold) {
        statsAppleCDP.qfFiltered++;
        return false;
    }

    return true;
}

void
AppleCDP::prefetchIssued(const PrefetchInfo &pfi, Addr paddr, int32_t priority,
                         const CacheAccessor &cache)
{
    (void)priority;
    auto pending_it = pendingPrefetches.find(blockAddress(pfi.getAddr()));
    if (pending_it == pendingPrefetches.end()) {
        return;
    }

    const PendingPrefetch meta = pending_it->second;
    rememberPrefetchRequest(paddr, meta);
    chargeQueuedPrefetch(pfi, meta);
    pendingPrefetches.erase(pending_it);

    if (!meta.adjacentLine && meta.issueAdjacentOnIssue) {
        RequestPtr adjacent_origin_req;
        std::unique_ptr<Packet> adjacent_origin_pkt;
        const Addr pc = meta.hasPC ? meta.pc : 0;

        if (useVirtualAddresses) {
            adjacent_origin_req = std::make_shared<Request>();
            adjacent_origin_req->setVirt(pfi.getAddr(), blkSize, meta.flags,
                                         meta.requestorId, pc);
            adjacent_origin_req->setPaddr(blockAddress(paddr));
        } else {
            adjacent_origin_req = std::make_shared<Request>(
                blockAddress(paddr), blkSize, meta.flags, meta.requestorId);
            if (meta.hasPC) {
                adjacent_origin_req->setPC(pc);
            }
        }
        if (meta.hasContextId) {
            adjacent_origin_req->setContext(meta.contextId);
        }

        adjacent_origin_pkt =
            std::make_unique<Packet>(adjacent_origin_req, MemCmd::ReadReq);
        PrefetchInfo adjacent_source_pfi(adjacent_origin_pkt.get(),
                                         pfi.getAddr(), true);
        queuePrefetch(adjacent_origin_pkt.get(), adjacent_source_pfi,
                      meta.adjacentAddr, meta.qfIndex, true, cache);
    }
}

void
AppleCDP::prefetchDropped(const PrefetchInfo &pfi, Addr paddr,
                          int32_t priority)
{
    (void)paddr;
    (void)priority;
    pendingPrefetches.erase(blockAddress(pfi.getAddr()));
}

bool
AppleCDP::processPrefetchFill(const PacketPtr &pkt, bool from_this_prefetcher,
                              PrefetchRequestEntry &matched_entry)
{
    const uint64_t hash = hashAddress(blockAddress(pkt->getAddr()));
    const size_t index = hash % prefetchRequestCache.size();
    PrefetchRequestEntry &entry = prefetchRequestCache[index];
    const Addr tag = hash / prefetchRequestCache.size();

    if (entry.valid && entry.tag == tag) {
        matched_entry = entry;
        if (!from_this_prefetcher) {
            entry.valid = false;
            return false;
        }
        creditPrefetch(entry.qfIndex, entry.adjacentLine);
        entry.valid = false;
        statsAppleCDP.requestCacheHits++;
        return true;
    } else {
        if (from_this_prefetcher) {
            globalQfCounter.reset();
            statsAppleCDP.requestCacheMisses++;
        }
        return false;
    }
}

void
AppleCDP::notifyFill(const CacheAccessProbeArg &arg)
{
    const PacketPtr pkt = arg.pkt;

    const bool from_this_prefetcher =
        pkt->req->requestorId() == requestorId &&
        (pkt->cmd.isHWPrefetch() || pkt->req->isPrefetch() ||
         pkt->req->taskId() == context_switch_task_id::Prefetcher);
    PrefetchRequestEntry matched_entry;
    const bool matched_prefetch_fill =
        processPrefetchFill(pkt, from_this_prefetcher, matched_entry);
    if (from_this_prefetcher && !matched_prefetch_fill) {
        return;
    }
    if ((from_this_prefetcher || matched_prefetch_fill) &&
        !scanPrefetchFills) {
        return;
    }

    if (!pkt->hasData() || !pkt->isRead() || pkt->req->isUncacheable()) {
        return;
    }
    if (pkt->req->isInstFetch() && !onInst) {
        return;
    }
    const bool has_pc =
        matched_prefetch_fill ? matched_entry.hasPC : pkt->req->hasPC();
    if (requirePC && !has_pc) {
        return;
    }
    if (!pkt->req->hasPaddr()) {
        return;
    }

    const Addr trigger_addr =
        matched_prefetch_fill ? matched_entry.triggerAddr
                              : ((useVirtualAddresses && pkt->req->hasVaddr())
                                     ? pkt->req->getVaddr()
                                     : pkt->req->getPaddr());
    const Addr pc =
        has_pc ? (matched_prefetch_fill ? matched_entry.pc : pkt->req->getPC())
               : 0;

    PacketPtr origin_pkt = pkt;
    RequestPtr recursive_origin_req;
    std::unique_ptr<Packet> recursive_origin_pkt;
    if (matched_prefetch_fill) {
        recursive_origin_req = std::make_shared<Request>();
        if (useVirtualAddresses) {
            recursive_origin_req->setVirt(trigger_addr, pkt->req->getSize(),
                                          matched_entry.flags,
                                          matched_entry.requestorId, pc);
            recursive_origin_req->setPaddr(matched_entry.triggerPaddr);
        } else {
            recursive_origin_req = std::make_shared<Request>(
                matched_entry.triggerPaddr, pkt->req->getSize(),
                matched_entry.flags, matched_entry.requestorId);
            if (has_pc) {
                recursive_origin_req->setPC(pc);
            }
        }
        if (matched_entry.hasContextId) {
            recursive_origin_req->setContext(matched_entry.contextId);
        }
        recursive_origin_pkt =
            std::make_unique<Packet>(recursive_origin_req, MemCmd::ReadReq);
        origin_pkt = recursive_origin_pkt.get();
    }

    PrefetchInfo source_pfi(origin_pkt, trigger_addr, true);

    const uint8_t *line = pkt->getConstPtr<uint8_t>();
    const unsigned line_size = pkt->getSize();
    scannedFillCount++;
    statsAppleCDP.fillsScanned++;

    if (qfResetInterval != 0 && (scannedFillCount % qfResetInterval) == 0) {
        resetQfCounters();
    }

    const unsigned pc_idx = pcIndex(pc);
    const unsigned slots = blkSize / scanGranularity;

    for (unsigned offset = 0; offset + pointerBytes <= line_size;
         offset += scanGranularity) {
        Addr candidate = 0;
        if (!detectPointerCandidate(line, line_size, offset, trigger_addr,
                                    candidate)) {
            continue;
        }

        statsAppleCDP.pointerCandidates++;

        if (historyFilterHit(candidate)) {
            statsAppleCDP.historyFiltered++;
            continue;
        }

        if (globalQfCounter < globalThreshold) {
            statsAppleCDP.globalFiltered++;
            continue;
        }

        const unsigned candidate_slot = (offset / scanGranularity) % slots;
        const QFIndex idx{pc_idx,
                          relativeOffset(trigger_addr, candidate_slot)};
        SatCounter8 &qf_counter = qfCounters[qfFlatIndex(idx)];
        if (qf_counter < qfCounterThreshold) {
            statsAppleCDP.qfFiltered++;
            continue;
        }

        const bool issue_adjacent =
            enableAdjacentLine &&
            adjacentLineCounters[pc_idx] >= adjacentLineThreshold;

        const Addr adjacent_addr =
            candidate + static_cast<int64_t>(adjacentLineDistance) * blkSize;
        queuePrefetch(origin_pkt, source_pfi, candidate, idx, false, arg.cache,
                      issue_adjacent, adjacent_addr);
    }
}

} // namespace prefetch
} // namespace gem5
