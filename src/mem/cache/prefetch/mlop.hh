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

/**
 * Implementation of the Multi-Lookahead Offset Prefetcher (MLOP).
 *
 * Reference:
 *   Shakerinava, M., Bakhshalipour, M., Lotfi-Kamran, P., &
 *   Sarbazi-Azad, H. Multi-Lookahead Offset Prefetching. DPC3, 2019.
 */

#ifndef __MEM_CACHE_PREFETCH_MLOP_HH__
#define __MEM_CACHE_PREFETCH_MLOP_HH__

#include <algorithm>
#include <cstdint>
#include <deque>
#include <vector>

#include "mem/cache/cache_probe_arg.hh"
#include "mem/cache/prefetch/queued.hh"
#include "mem/packet.hh"

namespace gem5
{

struct MLOPrefetcherParams;

namespace prefetch
{

class MLOPrefetcher : public Queued
{
  private:
    enum class BlockState : uint8_t
    {
        Init,
        Access,
        Prefetch
    };

    enum class FillLevel : uint8_t
    {
        None = 0,
        L1 = 1,
        L2 = 2,
        LLC = 3
    };

    struct ZoneKey
    {
        Addr zone;
        bool secure;

        bool
        operator==(const ZoneKey &other) const
        {
            return zone == other.zone && secure == other.secure;
        }
    };

    struct AccessMapEntry
    {
        ZoneKey key;
        bool valid;
        std::vector<BlockState> states;
        std::vector<uint8_t> prefetchFillLevels;
        std::deque<unsigned> recentAccesses;
        uint64_t lruTick;

        AccessMapEntry(unsigned blocks_per_zone)
            : key{0, false},
              valid(false),
              states(blocks_per_zone, BlockState::Init),
              prefetchFillLevels(blocks_per_zone, 0),
              recentAccesses(),
              lruTick(0)
        {}

        void
        reset(const ZoneKey &new_key)
        {
            key = new_key;
            valid = true;
            std::fill(states.begin(), states.end(), BlockState::Init);
            std::fill(prefetchFillLevels.begin(), prefetchFillLevels.end(), 0);
            recentAccesses.clear();
            lruTick = 0;
        }
    };

    const unsigned accessMapEntries;
    const unsigned accessMapWays;
    const unsigned accessMapSets;
    const unsigned accessMapIndexBits;
    const unsigned lookaheadLevels;
    const unsigned trainingAccesses;
    const unsigned l1ScoreThresholdPct;
    const unsigned l2ScoreThresholdPct;
    const unsigned maxPrefetchesPerAccess;
    const Addr zoneSize;
    const unsigned blocksPerZone;
    const int maxOffset;
    const unsigned numOffsets;
    const unsigned l1ScoreThreshold;
    const unsigned l2ScoreThreshold;

    uint64_t accessMapTick;
    std::vector<std::vector<AccessMapEntry>> accessMapTable;

    std::vector<std::vector<unsigned>> offsetScores;
    std::vector<std::vector<int>> selectedOffsets;
    std::vector<FillLevel> selectedFillLevels;
    unsigned updateCount;

    ZoneKey makeZoneKey(Addr line, bool secure) const;
    unsigned zoneOffset(Addr line) const;
    int offsetIndex(int offset) const;
    unsigned calcIndexBits(unsigned sets) const;
    Addr hashZone(Addr zone) const;
    unsigned setIndex(const ZoneKey &key) const;
    int32_t encodePriority(unsigned level, FillLevel fill_level) const;
    FillLevel decodeFillLevel(int32_t priority) const;

    AccessMapEntry *findEntry(const ZoneKey &key);
    AccessMapEntry &getOrCreateEntry(const ZoneKey &key);
    void touchEntry(AccessMapEntry &entry);
    void eraseEntryIfEmpty(AccessMapEntry &entry);

    void mark(Addr line, bool secure, BlockState state,
              FillLevel fill_level = FillLevel::None);
    void train(Addr line, bool secure);
    void finishTrainingRound();

    void notifyPacketAccepted(const PrefetchInfo &pfi, int32_t priority,
                              const PacketPtr &pkt) override;
    void notifyEvict(const CacheDataUpdateProbeArg &info) override;

  public:
    MLOPrefetcher(const MLOPrefetcherParams &p);
    ~MLOPrefetcher() = default;

    void calculatePrefetch(const PrefetchInfo &pfi,
                           std::vector<AddrPriority> &addresses,
                           const CacheAccessor &cache) override;
};

} // namespace prefetch
} // namespace gem5

#endif // __MEM_CACHE_PREFETCH_MLOP_HH__
