/*
 * Copyright (c) 2022-2023 The University of Edinburgh
 * All rights reserved
 *
 * The license below extends only to copyright in the software and shall
 * not be construed as granting a license to any other intellectual
 * property including but not limited to intellectual property relating
 * to a hardware implementation of the functionality of the software
 * licensed hereunder.  You may use the software subject to the license
 * terms below provided that you ensure that this notice is replicated
 * unmodified and in its entirety in all distributions of the software,
 * modified or unmodified, in source code or in binary form.
 *
 * Copyright (c) 2004-2005 The Regents of The University of Michigan
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

#include "cpu/pred/segmented_btb.hh"

#include "base/intmath.hh"
#include "base/trace.hh"
#include "debug/BTB.hh"

namespace gem5::branch_prediction
{

SegmentedBTB::SegmentedBTB(const SegmentedBTBParams &p)
    : BranchTargetBuffer(p),
      bm("BTB Monitor", p.bmEntries, p.bmAssoc, p.bmReplPolicy,
         p.bmIndexingPolicy, BMEntry(genTagExtractor(p.bmIndexingPolicy))),
      pageCache("Page Cache", p.pageCacheEntries, p.pageCacheAssoc,
                p.pageCacheReplPolicy, p.pageCacheIndexingPolicy,
                PageCacheEntry(genTagExtractor(p.pageCacheIndexingPolicy))),
      offsetCache(
          "Offset Cache", p.offsetCacheEntries, p.offsetCacheAssoc,
          p.offsetCacheReplPolicy, p.offsetCacheIndexingPolicy,
          OffsetCacheEntry(genTagExtractor(p.offsetCacheIndexingPolicy))),
      deltaCache(
          "Page Cache", p.deltaCacheEntries, p.deltaCacheAssoc,
          p.deltaCacheReplPolicy, p.deltaCacheIndexingPolicy,
          OffsetCacheEntry(genTagExtractor(p.deltaCacheIndexingPolicy))),
      targetCache(
          "Target Cache", p.targetCacheEntries, p.targetCacheAssoc,
          p.targetCacheReplPolicy, p.targetCacheIndexingPolicy,
          TargetCacheEntry(genTagExtractor(p.targetCacheIndexingPolicy))),
      rmTCThrshold(p.rmTCThrshold),
      hcReplThrshold(p.hcReplThrshold),
      tc2hcThrshold(p.tc2hcThrshold),
      hcInitUsefulness(p.rmTCThrshold / 2),
      tcInitUsefulness(p.tc2hcThrshold / 2),
      cacheStats(this)
{
    DPRINTF(BTB, "BTB: Creating Segmented BTB object.\n");
    gem5_assert(p.offsetCacheEntries < (1 << 12),
                "Offset Cache Entries should be less than 2^12.");
}

void
SegmentedBTB::memInvalidate()
{
    // Hot Cache Clear
    bm.clear();
    pageCache.clear();
    offsetCache.clear();
    deltaCache.clear();

    // Target Cache Clear
    targetCache.clear();
}

bool
SegmentedBTB::valid(ThreadID tid, Addr instPC)
{
    BMEntry *bmEntry = bm.findEntry({instPC, tid});
    if (!bmEntry) {
        return targetCache.findEntry({instPC, tid});
    }

    auto idx = bmEntry->entryID;
    if (bmEntry->flag) {
        auto dcEntry = deltaCache.findEntry({idx.delta, tid});
        return dcEntry != nullptr;
    } else {
        auto pcEntry = pageCache.findEntry({idx.page, tid});
        auto ocEntry = offsetCache.findEntry({idx.offset, tid});
        return pcEntry != nullptr && ocEntry != nullptr;
    }
}

const PCStateBase *
SegmentedBTB::hotCacheLookup(ThreadID tid, Addr instPC)
{
    cacheStats.bm.lookups++;
    BMEntry *entry = bm.accessEntry({instPC, tid});
    if (!entry) {
        // miss
        DPRINTF(BTB, "not found matched BTB Monitor for pc 0x%lx", instPC);
        return nullptr;
    }
    cacheStats.bm.hits++;

    // BM Hit
    auto id = entry->entryID;
    auto pcState = entry->target.get();
    Addr npc;

    if (!entry->flag) {
        cacheStats.pc.lookups++;
        cacheStats.oc.lookups++;
        // lookup page/offset cache by entryID (low bits index, high bits tag)
        auto ocEntry = getMostUseful(offsetCache, {id.offset, tid});
        auto pcEntry = getMostUseful(pageCache, {id.page, tid});

        if (ocEntry) {
            cacheStats.oc.hits++;
        }
        if (pcEntry) {
            cacheStats.pc.hits++;
        }

        if (!ocEntry || !pcEntry) {
            return nullptr;
        }
        source_maps[instPC] = TargetSource::PageOffsetCache;
        npc = (id.page << 12) + id.offset;
        DPRINTF(BTB,
                "Page/Offset Cache Lookup:"
                "pc=0x%lx, target=0x%x (pageID:0x%lx, offset:0x%lx).\n",
                instPC, npc, id.page, id.offset);
    } else {
        cacheStats.dc.lookups++;
        // lookup delta cache by entryID (low bits index, high bits tag)
        auto dcEntry = getMostUseful(deltaCache, {id.delta, tid});
        if (!dcEntry) {
            return nullptr;
        }

        cacheStats.dc.hits++;

        source_maps[instPC] = TargetSource::DeltaCache;
        npc = (getPGN(instPC) << 12) + id.delta;
        DPRINTF(BTB,
                "Delta Cache Lookup:"
                "pc=0x%x, target=0x%x (delta:0x%lx).\n",
                instPC, npc, id.delta);
    }

    // update pcstate target with hot cache target.
    pcState->set(npc);

    return pcState;
}

const PCStateBase *
SegmentedBTB::targetCacheLookup(ThreadID tid, Addr instPC)
{
    cacheStats.tc.lookups++;
    TargetCacheEntry *entry = targetCache.accessEntry({instPC, tid});
    if (entry) {
        cacheStats.tc.hits++;
        DPRINTF(BTB, "Target Cache Lookup, pc=0x%lx:target=0x%lx\n", instPC,
                entry->target->instAddr());
        source_maps[instPC] = TargetSource::TargetCache;
        return entry->target.get();
    }
    // miss
    return nullptr;
}

// @todo Create some sort of return struct that has both whether or not the
// address is valid, and also the address.  For now will just use addr = 0 to
// represent invalid entry.
const PCStateBase *
SegmentedBTB::lookup(ThreadID tid, Addr instPC, BranchType type)
{
    const PCStateBase *target;
    stats.lookups[type]++;
    DPRINTF(BTB, "Lookup for Prediction for pc=0x%lx.\n", instPC);

    if ((target = hotCacheLookup(tid, instPC))) {
        return target;
    }

    if ((target = targetCacheLookup(tid, instPC))) {
        return target;
    }

    DPRINTF(BTB, "BTB Target not found for pc=0x%lx.\n", instPC);
    // Miss
    stats.misses[type]++;
    source_maps[instPC] = TargetSource::Miss;
    return nullptr;
}

const StaticInstPtr
SegmentedBTB::getInst(ThreadID tid, Addr instPC)
{
    panic("not implemented.");
    // BTBEntry *entry = btb.findEntry({instPC, tid});

    // if (entry) {
    //     return entry->inst;
    // }
    //
    // return nullptr;
}

void
SegmentedBTB::update(ThreadID tid, Addr instPC, const PCStateBase &target,
                     BranchType type, StaticInstPtr inst)
{
    TargetSource source;

    if (source_maps[instPC]) {
        source = source_maps[instPC];
    } else {
        source = TargetSource::Miss;
    }
    DPRINTF(BTB, "update(pc=0x%lx): target 0x%x, source %d.\n", instPC,
            target.instAddr(), source);

    stats.updates[type]++;

    BMEntry *bmEntry = bm.findEntry({instPC, tid});
    TargetCacheEntry *tcEntry = targetCache.findEntry({instPC, tid});

    OffsetCacheEntry *ocEntry = nullptr;
    PageCacheEntry *pcEntry = nullptr;
    OffsetCacheEntry *dcEntry = nullptr;
    HotCacheEntryID hcid;

    if (bmEntry) {
        hcid = bmEntry->entryID;
    }

    // Target Hit
    switch (source) {
        case TargetSource::PageOffsetCache:
            // BMEntry may be invalid, for being replaced by other inst.
            if (!bmEntry) {
                break;
            }

            // Offset Cache Entry may be invalid, for being replaced by other
            // inst.
            ocEntry = offsetCache.findEntry({hcid.offset, tid});
            if (!ocEntry) {
                break;
            }

            // Page Cache Entry may be invalid, for being replaced by other
            // inst.
            pcEntry = pageCache.findEntry({hcid.page, tid});
            if (!pcEntry) {
                break;
            }

            if (target.instAddr() != bmEntry->target->instAddr()) {
                cacheStats.bm.mispredicted++;
                DPRINTF(BTB,
                        "page/offset cache source mispredicted: true target "
                        "0x%lx, cache target 0x%lx.\n",
                        target.instAddr(), bmEntry->target->instAddr());
                Addr actualPGN = getPGN(target.instAddr());
                Addr cachedPGN = getPGN(bmEntry->target->instAddr());
                if (actualPGN != cachedPGN) {
                    DPRINTF(BTB,
                            "page cache mispredicted: actual PGN 0x%lx, BM "
                            "PGN 0x%lx, PC PGN 0x%lx.\n",
                            actualPGN, cachedPGN, bmEntry->entryID.page);
                    cacheStats.pc.mispredicted++;
                }
                Addr actualOff = getPGOff(target.instAddr());
                Addr cachedOff = getPGOff(bmEntry->target->instAddr());
                if (actualOff != cachedOff) {
                    DPRINTF(BTB,
                            "offset cache mispredicted: actual Offset 0x%lx, "
                            "cached Offset 0x%lx.\n",
                            actualOff, cachedOff);
                    cacheStats.oc.mispredicted++;
                }
                break;
            }

            // NOTE: Target cache entry may be replaced by other branch.
            //       If so, just ignore this removing.
            if (tcEntry && pcEntry->usefulness == rmTCThrshold &&
                ocEntry->usefulness == rmTCThrshold) {
                tcEntry->invalidate();
                cacheStats.tc.updates++;
                cacheStats.removeFromTC++;
                DPRINTF(BTB,
                        "target 0x%lx(pc@0x%lx) usefulness "
                        "is high enough, removed entry in TargetCache",
                        target.instAddr(), instPC);
            }

            cacheStats.pc.updates++;
            cacheStats.oc.updates++;

            incCtr(pcEntry, rmTCThrshold);
            incCtr(ocEntry, rmTCThrshold);
            DPRINTF(BTB, "update page cache counter to %d.\n",
                    pcEntry->usefulness);
            DPRINTF(BTB, "update offset cache counter to %d.\n",
                    ocEntry->usefulness);

            return;
        case TargetSource::DeltaCache:
            // BMEntry may be invalid, for being replaced by other inst.
            if (!bmEntry) {
                break;
            }

            // Delta Cache Entry may be invalid, for being replaced by other
            // inst.
            dcEntry = deltaCache.findEntry({hcid.delta, tid});
            if (!dcEntry) {
                break;
            }

            if (target.instAddr() != bmEntry->target->instAddr()) {
                cacheStats.bm.mispredicted++;
                DPRINTF(BTB,
                        "delta cache source mispredicted: true target 0x%lx, "
                        "cache target 0x%lx.\n",
                        target.instAddr(), bmEntry->target->instAddr());
                if (getPGOff(target.instAddr()) !=
                    getPGOff(bmEntry->target->instAddr())) {
                    cacheStats.dc.mispredicted++;
                }
                break;
            }

            // NOTE: Target cache entry may be replaced by other branch.
            //       If so, just ignore this removing.
            if (tcEntry && dcEntry->usefulness == rmTCThrshold) {
                tcEntry->invalidate();
                cacheStats.tc.updates++;
                cacheStats.removeFromTC++;
                DPRINTF(BTB,
                        "target 0x%lx(pc@0x%lx) usefulness "
                        "is high enough, removed entry in TargetCache",
                        target, instPC);
            }

            cacheStats.dc.updates++;
            incCtr(dcEntry, rmTCThrshold);
            DPRINTF(BTB, "update delta cache counter to %d.\n",
                    dcEntry->usefulness);

            return;
        case TargetSource::TargetCache:
            // TargetCacheEntry may be invalid, for being replaced by other
            // inst.
            if (!tcEntry) {
                DPRINTF(BTB, "target cache source has been replaced.\n");
                break;
            }
            if (target.instAddr() != tcEntry->target->instAddr()) {
                cacheStats.tc.mispredicted++;
                DPRINTF(BTB,
                        "target cache source mispredicted: true target 0x%lx, "
                        "cache target 0x%lx.\n",
                        target.instAddr(), tcEntry->target->instAddr());
                break;
            }

            cacheStats.tc.updates++;
            incCtr(tcEntry, tc2hcThrshold);
            DPRINTF(BTB, "update target cache counter to %d.\n",
                    tcEntry->usefulness);

            if (tcEntry->usefulness == tc2hcThrshold) {
                hotCacheAlloc(tid, instPC, target, type, inst);
                DPRINTF(BTB,
                        "target 0x%lx(pc@0x%lx) usefulness "
                        "is high enough, try to alloc entry in HotCache",
                        target.instAddr(), instPC);
            }
            return;
        case TargetSource::Miss:
            break;
        default:
            panic("invalid Target Source: %d", source);
    }

    // target predicted failed.
    source_maps[instPC] = TargetSource::Miss;
    DPRINTF(BTB, "target 0x%lx(pc@0x%lx) miss in Any Cache", target.instAddr(),
            instPC);

    // Allocation in Target Cache
    TargetCacheEntry *tcVictim = targetCache.findVictim({instPC, tid});

    if (tcVictim->target) {
        DPRINTFR(BTB, "\tAllocate entry in TargetCache (0x%lx -> 0x%lx)\n",
                 tcVictim->target->instAddr(), target.instAddr());
    } else {
        DPRINTFR(BTB, "\tAllocate entry in TargetCache (first alloc 0x%lx)\n",
                 target.instAddr());
    }

    targetCache.insertEntry({instPC, tid}, tcVictim);
    tcVictim->update(target, inst);
    tcVictim->usefulness = tcInitUsefulness;
    cacheStats.tc.updates++;

    // For Direct Branch,
    // Check Delta Cache Or Page/Offset Cache For Allocation

    // Allocation Failed
}

bool
SegmentedBTB::hotCacheAlloc(ThreadID tid, Addr instPC,
                            const PCStateBase &target, BranchType type,
                            StaticInstPtr inst)
{

    DPRINTF(BTB, "try to allocate hot cache entry for target 0x%lx(pc@0x%lx)",
            target.instAddr(), instPC);

    auto bmCans = bm.getPossibleEntries({instPC, tid});
    auto biaPN = getPGN(instPC);
    auto btaPN = getPGN(target.instAddr());
    auto btaOff = getPGOff(target.instAddr());
    auto inSamePg = biaPN == btaPN;

    HotCacheTagEntry::KeyType pgKey = {btaPN, tid};
    HotCacheTagEntry::KeyType offKey = {btaOff, tid};

    BMEntry *bmCan = nullptr;
    PageCacheEntry *pcCan = nullptr;
    OffsetCacheEntry *ocCan = nullptr;
    OffsetCacheEntry *dcCan = nullptr;
    PageCacheEntry *oldPCCan = nullptr;
    OffsetCacheEntry *oldOCCan = nullptr;
    OffsetCacheEntry *oldDCCan = nullptr;

    // try to find empty BM Entry.
    for (auto can = bmCans.begin(); can != bmCans.end(); can++) {
        if (!(*can)->isValid()) {
            bmCan = *can;
            break;
        }
    }

    // try to find evictable BM Entry.
    if (!bmCan) {
        DPRINTFR(
            BTB,
            "\tdid not find empty BM Entry, try to find evictable BM Entry\n");
        for (auto can = bmCans.begin(); can != bmCans.end(); can++) {
            gem5_assert((*can)->isValid(),
                        "all BM Candidates should be valid.");
            auto hcid = (*can)->entryID;
            if ((*can)->flag) {
                oldDCCan =
                    cacheAlloc(deltaCache, {hcid.delta, tid}, hcReplThrshold);
                if (!oldDCCan) {
                    continue;
                }
                bmCan = *can;
                break;
            } else {
                oldPCCan =
                    cacheAlloc(pageCache, {hcid.page, tid}, hcReplThrshold);
                oldOCCan = cacheAlloc(offsetCache, {hcid.offset, tid},
                                      hcReplThrshold);
                if (!oldPCCan || !oldOCCan) {
                    continue;
                }
                bmCan = *can;
                break;
            }
        }
    } else {
        DPRINTFR(BTB, "\tfound empty BM Entry!!!\n");
    }

    // all hot cache entry has high confidence
    if (!bmCan) {
        DPRINTFR(BTB, "\tdid not find evictable BM Entry, panish hot cache "
                      "entry in BM Candidates.\n");
        for (auto can = bmCans.begin(); can != bmCans.end(); can++) {
            gem5_assert((*can)->isValid(),
                        "all BM Candidates should be valid.");
            auto hcid = (*can)->entryID;
            if ((*can)->flag) {
                DPRINTFR(BTB, "\tpanish delta cache @delta 0x%lx.\n",
                         hcid.delta);
                cachePanish(deltaCache, {hcid.delta, tid});
            } else {
                DPRINTFR(BTB, "\tpanish page cache @pgn 0x%lx.\n", hcid.page);
                cachePanish(pageCache, {hcid.page, tid});
                DPRINTFR(BTB, "\tpanish offset cache @offset 0x%lx.\n",
                         hcid.offset);
                cachePanish(offsetCache, {hcid.offset, tid});
            }
        }
        return false;
    }

    // find evictable hot cache entry and update hot cache entry.
    DPRINTFR(BTB, "\ttry to find evictable HotCacheEntry.");
    if (inSamePg) {
        dcCan = cacheAlloc(deltaCache, offKey, hcReplThrshold);
        if (!dcCan) {
            DPRINTFR(BTB,
                     "\tdid not find evictable deltaCacheEntry,"
                     "panish deltaCacheEntry index@0x%lx.\n",
                     btaOff);
            cachePanish(deltaCache, offKey);
            return false;
        }
        DPRINTFR(BTB, "\tfound evictable DeltaCacheEntry!!!");
    } else {
        pcCan = cacheAlloc(pageCache, pgKey, hcReplThrshold);
        if (!pcCan) {
            DPRINTFR(BTB,
                     "\tdid not find evictable pageCacheEntry,"
                     "panish pageCacheEntry index@0x%lx.\n",
                     btaPN);
            cachePanish(pageCache, pgKey);
            return false;
        }
        ocCan = cacheAlloc(offsetCache, offKey, hcReplThrshold);
        if (!ocCan) {
            DPRINTFR(BTB,
                     "\tdid not find evictable offsetCacheEntry,"
                     "panish offsetCacheEntry index@0x%lx.\n",
                     btaOff);
            cachePanish(offsetCache, offKey);
            return false;
        }
        DPRINTFR(BTB, "\tfound evictable Page/OffsetCacheEntry!!!");
    }

    // update bm entry.
    if (bmCan->isValid()) {
        // invalidate old hot cache entry.
        if (bmCan->flag) {
            oldDCCan->invalidate();
        } else {
            oldPCCan->invalidate();
            oldOCCan->invalidate();
        }
    }

    // update DeltaCache entry.
    if (dcCan) {
        gem5_assert(!pcCan && !ocCan,
                    "Page/OffsetCacheCan should be nullptr.");
        DPRINTF(BTB, "update deltaCacheEntry with new delta 0x%lx", btaOff);
        deltaCache.invalidate(dcCan);
        deltaCache.insertEntry(offKey, dcCan);
        dcCan->usefulness = hcInitUsefulness;
        cacheStats.dc.updates++;
    }

    // update Page / Offset Cache entry.
    if (pcCan && ocCan) {
        gem5_assert(!dcCan, "DeltaCacheCan should be nullptr.");
        DPRINTFR(BTB,
                 "update Page/OffsetCacheEntry "
                 "with new pageID 0x%lx and new Offset 0x%lx\n",
                 btaPN, btaOff);

        pageCache.invalidate(pcCan);
        pageCache.insertEntry(pgKey, pcCan);
        pcCan->usefulness = hcInitUsefulness;
        cacheStats.pc.updates++;

        offsetCache.invalidate(ocCan);
        offsetCache.insertEntry(offKey, ocCan);
        ocCan->usefulness = hcInitUsefulness;
        cacheStats.oc.updates++;
    }

    // Here storing the whold offset into entryID is only for convinience.
    // In actual hardware, only index is stored into entryID.
    bm.invalidate(bmCan);
    bm.insertEntry({instPC, tid}, bmCan);
    bmCan->update(target, inst);
    if (inSamePg) {
        bmCan->entryID.delta = btaOff;
        bmCan->flag = true;
    } else {
        bmCan->entryID.page = btaPN;
        bmCan->entryID.offset = btaOff;
        bmCan->flag = false;
    }
    cacheStats.bm.updates++;

    return true;
}

SegmentedBTB::SegmentedBTBStats::CacheStatsItem::CacheStatsItem(
    SegmentedBTBStats *segBtb, const char *name)
    : statistics::Group(segBtb, name),
      ADD_STAT(lookups, statistics::units::Count::get(),
               "Number of BTB SubCache lookups"),
      ADD_STAT(updates, statistics::units::Count::get(),
               "Number of BTB SubCache updates"),
      ADD_STAT(hits, statistics::units::Count::get(),
               "Number of BTB SubCache hits"),
      ADD_STAT(hitRatio, statistics::units::Ratio::get(),
               "BTB SubCache Hit Ratio", hits / lookups),
      ADD_STAT(mispredicted, statistics::units::Count::get(),
               "Number BTB SubCache mispredictions."
               "No target found or target wrong")
{
    using namespace statistics;
    hitRatio.precision(6);
}

} // namespace gem5::branch_prediction
