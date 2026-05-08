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

#ifndef __MEM_CACHE_TAGS_INFINITE_TAGS_HH__
#define __MEM_CACHE_TAGS_INFINITE_TAGS_HH__

#include <cstddef>
#include <functional>
#include <list>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include "base/types.hh"
#include "mem/cache/cache_blk.hh"
#include "mem/cache/replacement_policies/replaceable_entry.hh"
#include "mem/cache/tags/base.hh"
#include "mem/packet.hh"
#include "params/InfiniteTags.hh"

namespace gem5
{

class InfiniteTags : public BaseTags
{
  private:
    using Blocks = std::list<CacheBlk>;
    using BlockIterator = Blocks::iterator;

    struct PairHash
    {
        template <class T1, class T2>
        std::size_t
        operator()(const std::pair<T1, T2> &p) const
        {
            return std::hash<T1>{}(p.first) ^ (std::hash<T2>{}(p.second) << 1);
        }
    };

    using TagHashKey = std::pair<Addr, bool>;
    using TagHash = std::unordered_map<TagHashKey, BlockIterator, PairHash>;

    static constexpr std::size_t BlockDataChunkBytes = 256 * 1024;

    const std::size_t blockDataChunkBlocks;
    std::size_t nextBlockDataOffset;

    Blocks blocks;
    std::vector<std::unique_ptr<uint8_t[]>> blockDataChunks;
    TagHash blockMap;
    std::vector<BlockIterator> invalidBlocks;

    void attachBlockData(CacheBlk &blk);
    void registerBlock(CacheBlk &blk);
    TagHashKey makeTagHashKey(const CacheBlk::KeyType &key) const;

  public:
    PARAMS(InfiniteTags);

    InfiniteTags(const Params &p);

    void tagsInit() override;
    CacheBlk *findBlock(const CacheBlk::KeyType &key) const override;
    ReplaceableEntry *findBlockBySetAndWay(int set, int way) const override;
    CacheBlk *findVictim(const CacheBlk::KeyType &key, const std::size_t size,
                         std::vector<CacheBlk *> &evict_blks,
                         const uint64_t partition_id = 0) override;
    CacheBlk *accessBlock(const PacketPtr pkt, Cycles &lat) override;
    Addr extractTag(const Addr addr) const override;
    void insertBlock(const PacketPtr pkt, CacheBlk *blk) override;
    void invalidate(CacheBlk *blk) override;
    void moveBlock(CacheBlk *src_blk, CacheBlk *dest_blk) override;
    Addr regenerateBlkAddr(const CacheBlk *blk) const override;
    bool anyBlk(std::function<bool(CacheBlk &)> visitor) override;
};

} // namespace gem5

#endif // __MEM_CACHE_TAGS_INFINITE_TAGS_HH__
