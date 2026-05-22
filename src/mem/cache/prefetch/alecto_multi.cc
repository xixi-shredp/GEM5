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

#include "mem/cache/prefetch/alecto_multi.hh"

#include <algorithm>

#include "base/logging.hh"
#include "mem/cache/prefetch/queued.hh"
#include "params/AlectoMultiPrefetchers.hh"

namespace gem5
{

namespace prefetch
{

AlectoMulti::AlectoStats::AlectoStats(statistics::Group *parent)
    : statistics::Group(parent),
      ADD_STAT(demandLookups, statistics::units::Count::get(),
               "Demand requests looked up by Alecto"),
      ADD_STAT(selectedChildren, statistics::units::Count::get(),
               "Child prefetchers selected to train on demand requests"),
      ADD_STAT(queuedCandidates, statistics::units::Count::get(),
               "Prefetch candidates accepted into child queues by Alecto"),
      ADD_STAT(duplicateFiltered, statistics::units::Count::get(),
               "Duplicate prefetch candidates filtered by Alecto"),
      ADD_STAT(
          sandboxConfirmations, statistics::units::Count::get(),
          "Issued prefetches confirmed by matching later demand requests"),
      ADD_STAT(stateUpdates, statistics::units::Count::get(),
               "Allocation-table epoch updates"),
      ADD_STAT(deadResets, statistics::units::Count::get(),
               "Allocation entries reset after dead-counter saturation")
{}

AlectoMulti::AlectoMulti(const AlectoMultiPrefetchersParams &p)
    : Base(p),
      alectoStats(this),
      prefetchers(p.prefetchers.begin(), p.prefetchers.end()),
      allocationTable(p.allocation_entries),
      sampleTable(p.sample_entries),
      sandboxTable(p.sandbox_entries),
      temporalPrefetcherIndices(p.temporal_prefetcher_indices.begin(),
                                p.temporal_prefetcher_indices.end()),
      lastChosenPf(0),
      numPrefetchers(prefetchers.size()),
      allocationEntries(p.allocation_entries),
      sampleEntries(p.sample_entries),
      sandboxEntries(p.sandbox_entries),
      epochAccesses(p.epoch_accesses),
      conservativeDegree(p.conservative_degree),
      maxAggressiveLevel(p.max_aggressive_degree),
      proficiencyThresholdPct(p.proficiency_threshold_pct),
      deficiencyThresholdPct(p.deficiency_threshold_pct),
      blockedEpochs(p.blocked_epochs),
      deadCounterThreshold(p.dead_counter_threshold)
{
    fatal_if(numPrefetchers == 0, "Alecto requires at least one child "
                                  "prefetcher.");
    fatal_if(allocationEntries == 0 || sampleEntries == 0 ||
                 sandboxEntries == 0,
             "Alecto table sizes must be non-zero.");
    fatal_if(epochAccesses == 0, "Alecto epoch_accesses must be non-zero.");
    fatal_if(conservativeDegree == 0, "Alecto conservative_degree must be "
                                      "non-zero.");
    fatal_if(proficiencyThresholdPct > 100 || deficiencyThresholdPct > 100,
             "Alecto accuracy thresholds must be percentages.");
    fatal_if(deficiencyThresholdPct > proficiencyThresholdPct,
             "Alecto deficiency threshold must not exceed proficiency "
             "threshold.");

    for (const auto pf_index : temporalPrefetcherIndices) {
        fatal_if(pf_index >= numPrefetchers,
                 "Alecto temporal prefetcher "
                 "index %u is outside the child prefetcher vector.",
                 pf_index);
    }

    for (auto *pf : prefetchers) {
        fatal_if(dynamic_cast<Queued *>(pf) == nullptr,
                 "Alecto currently requires all children to derive from "
                 "QueuedPrefetcher.");
    }
}

void
AlectoMulti::setParentInfo(System *sys, ProbeManager *pm, unsigned blk_size)
{
    Base::setParentInfo(sys, pm, blk_size);

    for (auto *pf : prefetchers) {
        fatal_if(pf->usesVirtualAddresses() != usesVirtualAddresses(),
                 "Alecto child prefetcher %s address domain does not match "
                 "wrapper address domain.",
                 pf->name());
        pf->setParentInfo(sys, nullptr, blk_size);
    }
}

size_t
AlectoMulti::pcIndex(Addr pc, unsigned entries) const
{
    Addr folded = pc ^ (pc >> 12) ^ (pc >> 24);
    return folded % entries;
}

size_t
AlectoMulti::addrIndex(Addr addr, bool secure) const
{
    Addr folded = (addr >> lBlkSize) ^ (addr >> (lBlkSize + 11));
    return (folded ^ static_cast<Addr>(secure)) % sandboxEntries;
}

bool
AlectoMulti::isTemporal(unsigned pf_index) const
{
    return std::find(temporalPrefetcherIndices.begin(),
                     temporalPrefetcherIndices.end(),
                     pf_index) != temporalPrefetcherIndices.end();
}

bool
AlectoMulti::hasAggressiveState(const AllocationEntry &entry) const
{
    for (const auto &state : entry.states) {
        if (state.kind == StateKind::IA) {
            return true;
        }
    }

    return false;
}

void
AlectoMulti::resetAllocationEntry(AllocationEntry &entry, Addr pc)
{
    entry.valid = true;
    entry.tag = pc;
    entry.states.assign(numPrefetchers, PrefetcherState());
}

void
AlectoMulti::resetSampleEntry(SampleEntry &entry, Addr pc, bool keep_dead)
{
    const uint32_t dead = keep_dead ? entry.dead : 0;
    entry.valid = true;
    entry.tag = pc;
    entry.issued.assign(numPrefetchers, 0);
    entry.confirmed.assign(numPrefetchers, 0);
    entry.demands = 0;
    entry.dead = dead;
}

void
AlectoMulti::resetSandboxEntry(SandboxEntry &entry, Addr blk_addr, bool secure)
{
    entry.valid = true;
    entry.tag = blk_addr;
    entry.secure = secure;
    entry.childValid.assign(numPrefetchers, false);
    entry.childPc.assign(numPrefetchers, 0);
}

void
AlectoMulti::resetStatesToUI(AllocationEntry &entry)
{
    for (auto &state : entry.states) {
        state.kind = StateKind::UI;
        state.level = 0;
    }
}

AlectoMulti::AllocationEntry &
AlectoMulti::allocationEntry(Addr pc)
{
    auto &entry = allocationTable[pcIndex(pc, allocationEntries)];
    if (!entry.valid || entry.tag != pc) {
        resetAllocationEntry(entry, pc);
    }
    return entry;
}

AlectoMulti::SampleEntry &
AlectoMulti::sampleEntry(Addr pc)
{
    auto &entry = sampleTable[pcIndex(pc, sampleEntries)];
    if (!entry.valid || entry.tag != pc) {
        resetSampleEntry(entry, pc, false);
    }
    return entry;
}

AlectoMulti::SandboxEntry *
AlectoMulti::findSandboxEntry(Addr blk_addr, bool secure)
{
    auto &entry = sandboxTable[addrIndex(blk_addr, secure)];
    if (!entry.valid || entry.tag != blk_addr || entry.secure != secure) {
        return nullptr;
    }
    return &entry;
}

AlectoMulti::SandboxEntry &
AlectoMulti::sandboxEntryForInsert(Addr blk_addr, bool secure)
{
    auto &entry = sandboxTable[addrIndex(blk_addr, secure)];
    if (!entry.valid || entry.tag != blk_addr || entry.secure != secure) {
        resetSandboxEntry(entry, blk_addr, secure);
    }
    return entry;
}

bool
AlectoMulti::allChildrenBlocked(const AllocationEntry &entry) const
{
    for (const auto &state : entry.states) {
        if (state.kind != StateKind::IB) {
            return false;
        }
    }
    return true;
}

bool
AlectoMulti::isQueuedByAnyChild(const PrefetchInfo &pfi) const
{
    for (auto *pf : prefetchers) {
        if (static_cast<Queued *>(pf)->hasQueued(pfi)) {
            return true;
        }
    }

    return false;
}

Base *
AlectoMulti::findChildByRequestorId(RequestorID requestor) const
{
    for (auto *pf : prefetchers) {
        if (pf->getRequestorId() == requestor) {
            return pf;
        }
    }

    return nullptr;
}

unsigned
AlectoMulti::degreeForState(const PrefetcherState &state) const
{
    if (state.kind == StateKind::IB) {
        return 0;
    }

    if (state.kind == StateKind::UI) {
        return conservativeDegree;
    }

    return conservativeDegree + static_cast<unsigned>(state.level) + 1;
}

void
AlectoMulti::updateConfirmations(Addr pc, Addr blk_addr, bool secure)
{
    auto *sandbox = findSandboxEntry(blk_addr, secure);
    if (sandbox == nullptr) {
        return;
    }

    bool any_valid = false;
    auto &sample = sampleEntry(pc);
    for (unsigned i = 0; i < numPrefetchers; ++i) {
        if (sandbox->childValid[i] && sandbox->childPc[i] == pc) {
            sample.confirmed[i]++;
            sandbox->childValid[i] = false;
            usefulPrefetches++;
            prefetchStats.pfUseful++;
            alectoStats.sandboxConfirmations++;
        }
        any_valid = any_valid || sandbox->childValid[i];
    }

    if (!any_valid) {
        sandbox->valid = false;
    }
}

void
AlectoMulti::recordIssuedPrefetch(Addr pc, unsigned pf_index, Addr blk_addr,
                                  bool secure)
{
    auto &sample = sampleEntry(pc);
    sample.issued[pf_index]++;

    auto &sandbox = sandboxEntryForInsert(blk_addr, secure);
    sandbox.childValid[pf_index] = true;
    sandbox.childPc[pf_index] = pc;
}

void
AlectoMulti::updateDeadCounter(AllocationEntry &alloc, SampleEntry &sample,
                               bool generated_prefetch)
{
    if (generated_prefetch) {
        if (sample.dead > 0) {
            sample.dead--;
        }
        return;
    }

    if (sample.dead < deadCounterThreshold) {
        sample.dead++;
    }

    if (sample.dead >= deadCounterThreshold) {
        resetStatesToUI(alloc);
        sample.dead = 0;
        alectoStats.deadResets++;
    }
}

void
AlectoMulti::updateAllocationStates(AllocationEntry &alloc,
                                    SampleEntry &sample)
{
    std::vector<bool> promote(numPrefetchers, false);
    std::vector<bool> deficient(numPrefetchers, false);
    bool promoted_any = false;
    bool promoted_non_temporal = false;

    for (unsigned i = 0; i < numPrefetchers; ++i) {
        auto &state = alloc.states[i];

        if (state.kind == StateKind::IB && state.level < 0) {
            state.level++;
            continue;
        }

        if (sample.issued[i] == 0) {
            continue;
        }

        const unsigned accuracy =
            (100 * sample.confirmed[i]) / sample.issued[i];

        if (state.kind == StateKind::UI) {
            if (accuracy >= proficiencyThresholdPct) {
                promote[i] = true;
                promoted_any = true;
                promoted_non_temporal =
                    promoted_non_temporal || !isTemporal(i);
            } else if (accuracy < deficiencyThresholdPct) {
                deficient[i] = true;
            }
        } else if (state.kind == StateKind::IA) {
            if (accuracy >= proficiencyThresholdPct) {
                if (state.level < static_cast<int>(maxAggressiveLevel)) {
                    state.level++;
                }
            } else if (accuracy < proficiencyThresholdPct) {
                if (state.level > 0) {
                    state.level--;
                } else {
                    state.kind = StateKind::UI;
                    state.level = 0;
                }
            }
        }
    }

    if (promoted_any) {
        for (unsigned i = 0; i < numPrefetchers; ++i) {
            auto &state = alloc.states[i];
            if (promote[i] && !(promoted_non_temporal && isTemporal(i))) {
                state.kind = StateKind::IA;
                state.level = 0;
            } else if (state.kind == StateKind::UI) {
                state.kind = StateKind::IB;
                state.level = 0;
            }
        }
    }

    if (!promoted_any) {
        for (unsigned i = 0; i < numPrefetchers; ++i) {
            if (deficient[i]) {
                auto &state = alloc.states[i];
                state.kind = StateKind::IB;
                state.level = -static_cast<int>(blockedEpochs);
            }
        }
    }

    const bool has_ia = hasAggressiveState(alloc);

    if (!has_ia || allChildrenBlocked(alloc)) {
        for (auto &state : alloc.states) {
            if (state.kind == StateKind::IB && state.level == 0) {
                state.kind = StateKind::UI;
            }
        }
    }

    resetSampleEntry(sample, sample.tag, true);
    alectoStats.stateUpdates++;
}

void
AlectoMulti::notify(const CacheAccessProbeArg &arg, const PrefetchInfo &pfi)
{
    const Addr pc = pfi.hasPC() ? pfi.getPC() : 0;
    const Addr demand_blk = blockAddress(pfi.getAddr());
    const bool secure = pfi.isSecure();

    alectoStats.demandLookups++;
    updateConfirmations(pc, demand_blk, secure);
    for (auto *pf : prefetchers) {
        static_cast<Queued *>(pf)->squash(pfi);
    }

    auto &alloc = allocationEntry(pc);
    auto &sample = sampleEntry(pc);
    sample.demands++;

    bool generated_prefetch = false;
    std::vector<Addr> accepted_this_demand;

    for (unsigned i = 0; i < numPrefetchers; ++i) {
        const unsigned degree = degreeForState(alloc.states[i]);
        if (degree == 0) {
            continue;
        }

        auto *queued = static_cast<Queued *>(prefetchers[i]);
        std::vector<Queued::AddrPriority> addresses;
        queued->calculatePrefetch(pfi, addresses, arg.cache);
        if (addresses.empty()) {
            continue;
        }

        alectoStats.selectedChildren++;
        unsigned accepted = 0;
        for (auto &addr_prio : addresses) {
            Addr pf_blk = blockAddress(addr_prio.first);

            const bool already_accepted =
                std::find(accepted_this_demand.begin(),
                          accepted_this_demand.end(),
                          pf_blk) != accepted_this_demand.end();
            if (already_accepted || pf_blk == demand_blk ||
                findSandboxEntry(pf_blk, secure) != nullptr) {
                alectoStats.duplicateFiltered++;
                continue;
            }

            PrefetchInfo new_pfi(pfi, pf_blk);
            if (isQueuedByAnyChild(new_pfi)) {
                alectoStats.duplicateFiltered++;
                continue;
            }
            const bool skip_this_cache =
                alloc.states[i].kind == StateKind::IA &&
                accepted >= conservativeDegree;
            const bool inserted =
                queued->insert(arg.pkt, new_pfi, addr_prio.second, arg.cache,
                               skip_this_cache);
            if (!inserted) {
                alectoStats.duplicateFiltered++;
                continue;
            }
            accepted_this_demand.push_back(pf_blk);
            generated_prefetch = true;
            accepted++;
            alectoStats.queuedCandidates++;

            if (accepted == degree) {
                break;
            }
        }
    }

    if (hasAggressiveState(alloc)) {
        updateDeadCounter(alloc, sample, generated_prefetch);
    } else if (generated_prefetch && sample.dead > 0) {
        sample.dead--;
    }

    if (sample.demands >= epochAccesses) {
        updateAllocationStates(alloc, sample);
    }
}

void
AlectoMulti::notifyFill(const CacheAccessProbeArg &arg)
{
    const RequestorID requestor = arg.pkt->req->requestorId();
    Base *child = findChildByRequestorId(requestor);
    if (child != nullptr) {
        child->notifyFill(arg);
    }
}

void
AlectoMulti::notifyEvict(const CacheDataUpdateProbeArg &info)
{
    for (auto *pf : prefetchers) {
        pf->notifyEvict(info);
    }
}

void
AlectoMulti::notifyPrefetchAccepted(PacketPtr pkt)
{
    auto pending = std::find_if(pendingIssuedPrefetches.begin(),
                                pendingIssuedPrefetches.end(),
                                [pkt](const PendingIssuedPrefetch &entry) {
                                    return entry.pkt == pkt;
                                });
    if (pending == pendingIssuedPrefetches.end()) {
        return;
    }

    recordIssuedPrefetch(pending->pc, pending->pfIndex, pending->blkAddr,
                         pending->secure);
    prefetchStats.pfIssued++;
    issuedPrefetches++;
    pendingIssuedPrefetches.erase(pending);
}

void
AlectoMulti::notifyPrefetchDropped(PacketPtr pkt)
{
    auto pending = std::find_if(pendingIssuedPrefetches.begin(),
                                pendingIssuedPrefetches.end(),
                                [pkt](const PendingIssuedPrefetch &entry) {
                                    return entry.pkt == pkt;
                                });
    if (pending != pendingIssuedPrefetches.end()) {
        pendingIssuedPrefetches.erase(pending);
    }
}

Tick
AlectoMulti::nextPrefetchReadyTime() const
{
    Tick next_ready = MaxTick;

    for (auto *pf : prefetchers) {
        next_ready = std::min(next_ready, pf->nextPrefetchReadyTime());
    }

    return next_ready;
}

PacketPtr
AlectoMulti::getPacket()
{
    for (unsigned attempts = 0; attempts < numPrefetchers; ++attempts) {
        lastChosenPf = (lastChosenPf + 1) % numPrefetchers;
        if (prefetchers[lastChosenPf]->nextPrefetchReadyTime() > curTick()) {
            continue;
        }

        auto *queued = static_cast<Queued *>(prefetchers[lastChosenPf]);
        Addr issued_addr = 0;
        Addr issued_pc = 0;
        bool issued_secure = false;
        PacketPtr pkt =
            queued->getPacket(&issued_addr, &issued_pc, &issued_secure);
        panic_if(!pkt, "Prefetcher is ready but didn't return a packet.");

        const Addr blk_addr = blockAddress(issued_addr);
        const bool secure = issued_secure;
        const Addr pc = issued_pc;

        if (findSandboxEntry(blk_addr, secure) != nullptr) {
            delete pkt;
            alectoStats.duplicateFiltered++;
            continue;
        }

        pendingIssuedPrefetches.push_back(
            {pkt, pc, lastChosenPf, blk_addr, secure});
        return pkt;
    }

    return nullptr;
}

void
AlectoMulti::prefetchUnused()
{
    Base::prefetchUnused();

    for (auto *pf : prefetchers) {
        pf->prefetchUnused();
    }
}

void
AlectoMulti::incrDemandMhsrMisses()
{
    Base::incrDemandMhsrMisses();

    for (auto *pf : prefetchers) {
        pf->incrDemandMhsrMisses();
    }
}

} // namespace prefetch
} // namespace gem5
