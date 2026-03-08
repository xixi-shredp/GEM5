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

#ifndef __CPU_PRED_SEGMENTED_BTB_HH__
#define __CPU_PRED_SEGMENTED_BTB_HH__

#include "base/cache/associative_cache.hh"
#include "base/logging.hh"
#include "base/types.hh"
#include "cpu/pred/btb.hh"
#include "cpu/pred/btb_entry.hh"
#include "mem/cache/replacement_policies/replaceable_entry.hh"
#include "mem/cache/tags/indexing_policies/base.hh"
#include "params/SegmentedBTB.hh"

namespace gem5::branch_prediction
{

class HotCacheTagEntry : public ReplaceableEntry
{
  public:
    using IndexingPolicy = BTBIndexingPolicy;
    using KeyType = BTBTagType::KeyType;
    using TagExtractor = std::function<Addr(Addr)>;

    /** Default constructor */
    HotCacheTagEntry(TagExtractor ext)
        : extractTag(ext), valid(false), tag({MaxAddr, -1})
    {}

    /**
     * Checks if the given tag information corresponds to this entry's.
     */
    bool
    match(const KeyType &key) const
    {
        return isValid() && (tag.address == extractTag(key.address));
    }

    /**
     * Insert the block by assigning it a tag and marking it valid. Touches
     * block if it hadn't been touched previously.
     */
    void
    insert(const KeyType &key)
    {
        setValid();
        setTag({extractTag(key.address), key.tid});
    }

    /** Copy constructor */
    HotCacheTagEntry(const HotCacheTagEntry &other)
    {
        valid = other.valid;
        tag = other.tag;
        extractTag = other.extractTag;
    }

    /** Assignment operator */
    HotCacheTagEntry &
    operator=(const HotCacheTagEntry &other)
    {
        valid = other.valid;
        tag = other.tag;
        extractTag = other.extractTag;

        return *this;
    }

    /**
     * Checks if the entry is valid.
     */
    bool
    isValid() const
    {
        return valid;
    }

    /**
     * Get tag associated to this block.
     */
    KeyType
    getTag() const
    {
        return tag;
    }

    /** Invalidate the block. Its contents are no longer valid. */
    void
    invalidate()
    {
        valid = false;
    }

    std::string
    print() const override
    {
        return csprintf("tag: %#x valid: %d | %s", tag.address, isValid(),
                        ReplaceableEntry::print());
    }

  protected:
    /**
     * Set tag associated to this block.
     */
    void
    setTag(KeyType _tag)
    {
        tag = _tag;
    }

    /** Set valid bit. The block must be invalid beforehand. */
    void
    setValid()
    {
        assert(!isValid());
        valid = true;
    }

  private:
    /** Callback used to extract the tag from the entry */
    TagExtractor extractTag;

    /**
     * Valid bit. The contents of this entry are only valid if this bit is set.
     * @sa invalidate()
     * @sa insert()
     */
    bool valid;

    /** The entry's tag. */
    KeyType tag;
};

typedef union
{
    uint16_t delta;
    struct
    {
        Addr page;
        uint16_t offset;
    };
} HotCacheEntryID;

class BMEntry : public BTBEntry
{
  public:
    /**
     * Used to index the delta cache or the page/offset cache.
     */
    HotCacheEntryID entryID;

    /**
     * Flag bit. This bit is used for distinguish the entry as belonging to
     * the delta cache or the page/offset caches. (true for delta cache)
     */
    bool flag;

    /**
     * The entry's target field in BTBEntry is used to store states.
     * (for compatibility with gem5)
     * This field doesn't exist in real hardware.
     */

  public:
    /** Default constructor */
    BMEntry(TagExtractor ext) : BTBEntry(ext) {}
};

struct PageCacheEntry : HotCacheTagEntry
{
  public:
    uint8_t usefulness;

  public:
    /** Default constructor */
    PageCacheEntry(TagExtractor ext) : HotCacheTagEntry(ext) {}
};

struct OffsetCacheEntry : HotCacheTagEntry
{
    uint8_t usefulness;

  public:
    /** Default constructor */
    OffsetCacheEntry(TagExtractor ext) : HotCacheTagEntry(ext) {}
};

class TargetCacheEntry : public BTBEntry
{
  public:
    uint8_t usefulness;

  public:
    /** Default constructor */
    TargetCacheEntry(TagExtractor ext) : BTBEntry(ext) {}
};

class SegmentedBTB : public BranchTargetBuffer
{
  private:
    typedef enum
    {
        Miss,
        PageOffsetCache,
        DeltaCache,
        TargetCache
    } TargetSource;

  public:
    SegmentedBTB(const SegmentedBTBParams &params);

    void memInvalidate() override;
    bool valid(ThreadID tid, Addr instPC) override;
    const PCStateBase *lookup(ThreadID tid, Addr instPC,
                              BranchType type = BranchType::NoBranch) override;
    void update(ThreadID tid, Addr instPC, const PCStateBase &target_pc,
                BranchType type = BranchType::NoBranch,
                StaticInstPtr inst = nullptr) override;
    const StaticInstPtr getInst(ThreadID tid, Addr instPC) override;

  private:
    /** BTB Monitor. */
    AssociativeCache<BMEntry> bm;

    AssociativeCache<PageCacheEntry> pageCache;

    AssociativeCache<OffsetCacheEntry> offsetCache;

    AssociativeCache<OffsetCacheEntry> deltaCache;

    AssociativeCache<TargetCacheEntry> targetCache;

    /**
     * Remove Target Cache Entry Usefulness Counter Thrshold
     * for Hot Cache Entry
     */
    unsigned rmTCThrshold;

    /**
     * Hot Cache Entry Eviction Usefulness Counter Thrshold.
     */
    unsigned hcReplThrshold;

    /**
     * Allocate Hot Cache Entry Usefulness Counter Thrshold
     * from Target Cache Entry.
     */
    unsigned tc2hcThrshold;

    /**
     * Initial Hot Cache Entry Usefulness Counter Value.
     */
    unsigned hcInitUsefulness;

    /**
     * Initial Target Cache Entry Usefulness Counter Value.
     */
    unsigned tcInitUsefulness;

    std::unordered_map<Addr, TargetSource> source_maps;

  private:
    const PCStateBase *hotCacheLookup(ThreadID tid, Addr instPC);
    const PCStateBase *targetCacheLookup(ThreadID tid, Addr instPC);

    /**
     * Allocate a entry for HotCacheEntry with updates.
     */
    bool hotCacheAlloc(ThreadID tid, Addr instPC, const PCStateBase &target_pc,
                       BranchType type, StaticInstPtr inst);

  protected:
    struct SegmentedBTBStats : statistics::Group
    {
        SegmentedBTBStats(BranchTargetBuffer *btb)
            : statistics::Group(btb, "segmented_btb"),
              bm(this, "btb_monitor"),
              pc(this, "page_cache"),
              oc(this, "offset_cache"),
              dc(this, "delta_cache"),
              tc(this, "target_cache"),
              ADD_STAT(removeFromTC, statistics::units::Count::get(),
                       "Number of removing from Target Cache")
        {}

        struct CacheStatsItem : public statistics::Group
        {
            CacheStatsItem(SegmentedBTBStats *segBtb, const char *name);
            statistics::Scalar lookups;
            statistics::Scalar updates;
            statistics::Scalar hits;
            statistics::Formula hitRatio;
            statistics::Scalar mispredicted;
        };
        /** BTB Monitor Statistics */
        struct CacheStatsItem bm;
        /** Page Cache Statistics */
        struct CacheStatsItem pc;
        /** Offset Cache Statistics */
        struct CacheStatsItem oc;
        /** Delta Cache Statistics */
        struct CacheStatsItem dc;
        /** Target Cache Statistics */
        struct CacheStatsItem tc;

        /** Number of removing from Target Cache */
        statistics::Scalar removeFromTC;

    } cacheStats;
};

template <typename Entry>
Entry *
getMostUseful(AssociativeCache<Entry> &cache,
              const HotCacheTagEntry::KeyType &key)
{
    auto cans = cache.getPossibleEntries(key);
    int mostUsefulness = -1;
    int mostUsefulid = -1;
    int id = 0;
    for (auto can = cans.begin(); can != cans.end(); can++) {
        if ((*can)->isValid() && ((*can)->usefulness > mostUsefulness)) {
            mostUsefulness = (*can)->usefulness;
            mostUsefulid = id;
        }
        id++;
    }
    if (mostUsefulid != -1) {
        return cans[mostUsefulid];
    } else {
        return nullptr;
    }
}

template <typename Entry>
static inline void
incCtr(Entry *entry, uint32_t max)
{
    if (entry->usefulness < max) {
        entry->usefulness++;
    }
}

template <typename Entry>
static inline void
decCtr(Entry *entry, uint32_t min = 0)
{
    if (entry->usefulness > min) {
        entry->usefulness--;
    }
}

template <typename Entry>
Entry *
cachePanish(AssociativeCache<Entry> &cache,
            const HotCacheTagEntry::KeyType &key)
{
    auto cans = cache.getPossibleEntries(key);
    bool hasValid = false;
    for (auto can = cans.begin(); can != cans.end(); can++) {
        // some hot cache entry may be invalid.
        if (!(*can)->isValid()) {
            continue;
        }
        hasValid = true;
        decCtr(*can);
    }
    gem5_assert(hasValid, "panished cache entries should have valid one.");
    return nullptr;
}

/**
 * Allocate a entry for HotCacheEntry without any updates.
 */
template <typename Entry>
Entry *
cacheAlloc(AssociativeCache<Entry> &cache,
           const HotCacheTagEntry::KeyType &key, uint32_t replCtr)
{
    auto cans = cache.getPossibleEntries(key);

    // find empty Cache Entry
    for (auto can = cans.begin(); can != cans.end(); can++) {
        if (!(*can)->isValid()) {
            return *can;
        }
    }

    // find evictable Cache Entry
    for (auto can = cans.begin(); can != cans.end(); can++) {
        if ((*can)->usefulness <= replCtr) {
            return *can;
        }
    }

    // cache entry allocation failed.
    return nullptr;
}

static inline Addr
getPGN(Addr addr)
{
    return addr >> 12;
}

static inline Addr
getPGOff(Addr addr)
{
    return addr & ((1ull << 12) - 1);
}

} // namespace gem5::branch_prediction

#endif // __CPU_PRED_SEGMENTED_BTB_HH__
