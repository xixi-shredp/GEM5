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

#include "mem/cache/prefetch/mlop.hh"

#include <algorithm>
#include <string>

#include "base/logging.hh"
#include "debug/HWPrefetch.hh"
#include "params/MLOPrefetcher.hh"

namespace gem5
{

namespace prefetch
{

namespace
{

Addr
validateZoneSize(Addr zone_size, unsigned blk_size, const std::string &name)
{
    fatal_if(zone_size < blk_size || (zone_size % blk_size) != 0,
             "%s: zone_size must be a multiple of the cache block size", name);

    const Addr blocks_per_zone = zone_size / blk_size;
    fatal_if(blocks_per_zone < 2,
             "%s: zone_size must cover at least two cache blocks", name);
    fatal_if(blocks_per_zone > 128,
             "%s: zone_size creates too many offsets for this implementation",
             name);

    return zone_size;
}

} // anonymous namespace

MLOPrefetcher::MLOPrefetcher(const MLOPrefetcherParams &p)
    : Queued(p),
      accessMapEntries(p.access_map_entries),
      accessMapWays(p.access_map_ways),
      accessMapSets(p.access_map_ways == 0
                        ? 0
                        : p.access_map_entries / p.access_map_ways),
      accessMapIndexBits(calcIndexBits(accessMapSets)),
      lookaheadLevels(p.lookahead_levels),
      trainingAccesses(p.training_accesses),
      l1ScoreThresholdPct(p.l1_score_threshold_pct),
      l2ScoreThresholdPct(p.l2_score_threshold_pct),
      maxPrefetchesPerAccess(p.max_prefetches_per_access),
      zoneSize(validateZoneSize(p.zone_size, blkSize, name())),
      blocksPerZone(zoneSize / blkSize),
      maxOffset(blocksPerZone - 1),
      numOffsets(2 * blocksPerZone - 1),
      l1ScoreThreshold((trainingAccesses * l1ScoreThresholdPct + 99) / 100),
      l2ScoreThreshold((trainingAccesses * l2ScoreThresholdPct + 99) / 100),
      accessMapTick(1),
      accessMapTable(accessMapSets,
                     std::vector<AccessMapEntry>(
                         accessMapWays, AccessMapEntry(blocksPerZone))),
      offsetScores(lookaheadLevels, std::vector<unsigned>(numOffsets, 0)),
      selectedOffsets(lookaheadLevels),
      selectedFillLevels(lookaheadLevels, FillLevel::None),
      updateCount(0)
{
    fatal_if(accessMapEntries == 0, "%s: access_map_entries must be > 0",
             name());
    fatal_if(accessMapWays == 0, "%s: access_map_ways must be > 0", name());
    fatal_if(accessMapEntries % accessMapWays != 0,
             "%s: access_map_entries must be a multiple of access_map_ways",
             name());
    fatal_if(lookaheadLevels == 0, "%s: lookahead_levels must be > 0", name());
    fatal_if(trainingAccesses == 0, "%s: training_accesses must be > 0",
             name());
    fatal_if(l1ScoreThresholdPct > 100,
             "%s: l1_score_threshold_pct must be <= 100", name());
    fatal_if(l2ScoreThresholdPct > l1ScoreThresholdPct,
             "%s: l2_score_threshold_pct must be <= l1_score_threshold_pct",
             name());
}

MLOPrefetcher::ZoneKey
MLOPrefetcher::makeZoneKey(Addr line, bool secure) const
{
    return ZoneKey{line / blocksPerZone, secure};
}

unsigned
MLOPrefetcher::zoneOffset(Addr line) const
{
    return line % blocksPerZone;
}

int
MLOPrefetcher::offsetIndex(int offset) const
{
    assert(offset != 0);
    assert(offset >= -maxOffset && offset <= maxOffset);
    return maxOffset + offset;
}

unsigned
MLOPrefetcher::calcIndexBits(unsigned sets) const
{
    if (sets <= 1) {
        return 0;
    }

    unsigned bits = 0;
    for (unsigned max_index = sets - 1; max_index > 0; max_index >>= 1) {
        bits++;
    }
    return bits;
}

Addr
MLOPrefetcher::hashZone(Addr zone) const
{
    if (accessMapIndexBits == 0) {
        return zone;
    }

    const Addr mask = (static_cast<Addr>(1) << accessMapIndexBits) - 1;
    Addr key = zone;
    for (Addr tag = key >> accessMapIndexBits; tag > 0;
         tag >>= accessMapIndexBits) {
        key ^= tag & mask;
    }
    return key;
}

unsigned
MLOPrefetcher::setIndex(const ZoneKey &key) const
{
    return hashZone(key.zone) % accessMapSets;
}

int32_t
MLOPrefetcher::encodePriority(unsigned level, FillLevel fill_level) const
{
    const unsigned fill = static_cast<unsigned>(fill_level);
    return static_cast<int32_t>(((lookaheadLevels - level) << 2) + (4 - fill));
}

MLOPrefetcher::FillLevel
MLOPrefetcher::decodeFillLevel(int32_t priority) const
{
    const unsigned encoded = static_cast<unsigned>(priority) & 0x3;
    const unsigned fill = 4 - encoded;
    if (fill >= static_cast<unsigned>(FillLevel::L1) &&
        fill <= static_cast<unsigned>(FillLevel::LLC)) {
        return static_cast<FillLevel>(fill);
    }
    return FillLevel::L2;
}

MLOPrefetcher::AccessMapEntry *
MLOPrefetcher::findEntry(const ZoneKey &key)
{
    auto &set = accessMapTable[setIndex(key)];
    for (auto &entry : set) {
        if (entry.valid && entry.key == key) {
            return &entry;
        }
    }
    return nullptr;
}

MLOPrefetcher::AccessMapEntry &
MLOPrefetcher::getOrCreateEntry(const ZoneKey &key)
{
    AccessMapEntry *entry = findEntry(key);
    if (entry != nullptr) {
        return *entry;
    }

    auto &set = accessMapTable[setIndex(key)];
    auto victim = set.begin();
    for (auto it = set.begin(); it != set.end(); it++) {
        if (!it->valid) {
            victim = it;
            break;
        }
        if (it->lruTick < victim->lruTick) {
            victim = it;
        }
    }

    victim->reset(key);
    return *victim;
}

void
MLOPrefetcher::touchEntry(AccessMapEntry &entry)
{
    entry.lruTick = accessMapTick++;
}

void
MLOPrefetcher::eraseEntryIfEmpty(AccessMapEntry &entry)
{
    for (BlockState state : entry.states) {
        if (state != BlockState::Init) {
            return;
        }
    }

    entry.valid = false;
    entry.recentAccesses.clear();
}

void
MLOPrefetcher::mark(Addr line, bool secure, BlockState state,
                    FillLevel fill_level)
{
    const ZoneKey key = makeZoneKey(line, secure);
    const unsigned offset = zoneOffset(line);

    if (state == BlockState::Init) {
        AccessMapEntry *entry = findEntry(key);
        if (entry == nullptr) {
            return;
        }

        entry->states[offset] = BlockState::Init;
        entry->prefetchFillLevels[offset] = 0;
        eraseEntryIfEmpty(*entry);
        return;
    }

    if (state == BlockState::Prefetch) {
        AccessMapEntry *entry = findEntry(key);
        if (entry == nullptr) {
            return;
        }

        if (entry->states[offset] != BlockState::Access) {
            entry->states[offset] = state;
            entry->prefetchFillLevels[offset] =
                static_cast<uint8_t>(fill_level);
        }
        return;
    }

    assert(state == BlockState::Access);
    AccessMapEntry &entry = getOrCreateEntry(key);
    entry.states[offset] = state;
    entry.prefetchFillLevels[offset] = 0;
    entry.recentAccesses.push_front(offset);
    while (entry.recentAccesses.size() > lookaheadLevels - 1) {
        entry.recentAccesses.pop_back();
    }
    touchEntry(entry);
}

void
MLOPrefetcher::train(Addr line, bool secure)
{
    const ZoneKey key = makeZoneKey(line, secure);
    AccessMapEntry *entry = findEntry(key);
    if (entry == nullptr) {
        return;
    }

    const unsigned current = zoneOffset(line);
    if (entry->states[current] == BlockState::Access) {
        return;
    }

    updateCount++;

    std::vector<BlockState> states = entry->states;
    const unsigned levels_to_update =
        std::min<unsigned>(lookaheadLevels, entry->recentAccesses.size() + 1);

    for (unsigned level = 0; level < levels_to_update; level++) {
        if (level != 0) {
            states[entry->recentAccesses[level - 1]] = BlockState::Init;
        }

        for (unsigned pos = 0; pos < blocksPerZone; pos++) {
            if (states[pos] != BlockState::Access || pos == current) {
                continue;
            }

            const int offset =
                static_cast<int>(current) - static_cast<int>(pos);
            if (offset >= -maxOffset && offset <= maxOffset) {
                offsetScores[level][offsetIndex(offset)]++;
            }
        }
    }

    if (updateCount >= trainingAccesses) {
        finishTrainingRound();
    }
}

void
MLOPrefetcher::finishTrainingRound()
{
    updateCount = 0;

    for (auto &offsets : selectedOffsets) {
        offsets.clear();
    }
    std::fill(selectedFillLevels.begin(), selectedFillLevels.end(),
              FillLevel::None);

    std::vector<bool> already_selected(numOffsets, false);

    /*
     * Select farther lookahead levels first, like the ChampSim reference,
     * so a long-lookahead offset is not later duplicated by a near level.
     */
    for (int level = lookaheadLevels - 1; level >= 0; level--) {
        const auto &scores = offsetScores[level];
        const unsigned best_score =
            *std::max_element(scores.begin(), scores.end());

        FillLevel fill_level = FillLevel::None;
        if (best_score >= l1ScoreThreshold) {
            fill_level = FillLevel::L1;
        } else if (best_score >= l2ScoreThreshold) {
            fill_level = FillLevel::L2;
        } else {
            continue;
        }

        for (int offset = -maxOffset; offset <= maxOffset; offset++) {
            if (offset == 0) {
                continue;
            }

            const int index = offsetIndex(offset);
            if (scores[index] == best_score && !already_selected[index]) {
                selectedOffsets[level].push_back(offset);
                already_selected[index] = true;
            }
        }
        selectedFillLevels[level] = fill_level;
    }

    for (auto &scores : offsetScores) {
        std::fill(scores.begin(), scores.end(), 0);
    }

    if (debug::HWPrefetch) {
        for (unsigned level = 0; level < lookaheadLevels; level++) {
            for (int offset : selectedOffsets[level]) {
                DPRINTF(HWPrefetch,
                        "MLOP selected offset %d for lookahead %u fill %u\n",
                        offset, level + 1,
                        static_cast<unsigned>(selectedFillLevels[level]));
            }
        }
    }
}

void
MLOPrefetcher::notifyPacketAccepted(const PrefetchInfo &pfi, int32_t priority,
                                    const PacketPtr &pkt)
{
    if (pkt == nullptr) {
        return;
    }

    mark(blockIndex(pfi.getAddr()), pfi.isSecure(), BlockState::Prefetch,
         decodeFillLevel(priority));
}

void
MLOPrefetcher::notifyEvict(const CacheDataUpdateProbeArg &info)
{
    mark(blockIndex(info.addr), info.isSecure, BlockState::Init);
}

void
MLOPrefetcher::calculatePrefetch(const PrefetchInfo &pfi,
                                 std::vector<AddrPriority> &addresses,
                                 const CacheAccessor &cache)
{
    const Addr addr = blockAddress(pfi.getAddr());
    const Addr line = blockIndex(addr);
    const bool secure = pfi.isSecure();
    const bool prefetched_hit =
        cache.hasBeenPrefetched(addr, secure, requestorId);

    if (pfi.isCacheMiss() || prefetched_hit) {
        train(line, secure);
    }

    mark(line, secure, BlockState::Access);

    AccessMapEntry *entry = findEntry(makeZoneKey(line, secure));
    if (entry == nullptr) {
        return;
    }

    const unsigned current = zoneOffset(line);
    unsigned generated = 0;

    for (unsigned level = 0; level < lookaheadLevels; level++) {
        const FillLevel fill_level = selectedFillLevels[level];
        if (fill_level == FillLevel::None) {
            continue;
        }

        for (int offset : selectedOffsets[level]) {
            const int target_offset = static_cast<int>(current) + offset;
            if (target_offset < 0 ||
                target_offset >= static_cast<int>(blocksPerZone)) {
                continue;
            }

            const auto target = static_cast<unsigned>(target_offset);
            if (entry->states[target] == BlockState::Access) {
                continue;
            }
            if (entry->states[target] == BlockState::Prefetch &&
                entry->prefetchFillLevels[target] <=
                    static_cast<uint8_t>(fill_level)) {
                continue;
            }

            const Addr pf_line = line + offset;
            const Addr pf_addr = pf_line << lBlkSize;

            addresses.push_back(
                AddrPriority(pf_addr, encodePriority(level, fill_level)));
            generated++;

            DPRINTF(HWPrefetch,
                    "MLOP generated prefetch %#lx offset %d lookahead %u "
                    "fill %u\n",
                    pf_addr, offset, level + 1,
                    static_cast<unsigned>(fill_level));

            if (generated == maxPrefetchesPerAccess) {
                return;
            }
        }
    }
}

} // namespace prefetch
} // namespace gem5
