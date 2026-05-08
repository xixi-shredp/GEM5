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

#include "mem/cache/tags/infinite_tags.hh"

#include <algorithm>
#include <cassert>
#include <cstdint>

#include "mem/cache/tags/partitioning_policies/partition_manager.hh"

namespace gem5
{

InfiniteTags::InfiniteTags(const Params &p)
    : BaseTags(p),
      blockDataChunkBlocks(
          std::max<std::size_t>(1, BlockDataChunkBytes / blkSize)),
      nextBlockDataOffset(0)
{
    blockMap.reserve(numBlocks);
    invalidBlocks.reserve(numBlocks);
}

void
InfiniteTags::tagsInit()
{}

void
InfiniteTags::attachBlockData(CacheBlk &blk)
{
    if (blockDataChunks.empty() ||
        nextBlockDataOffset == blockDataChunkBlocks) {
        blockDataChunks.emplace_back(
            new uint8_t[blockDataChunkBlocks * blkSize]);
        nextBlockDataOffset = 0;
    }

    blk.data = blockDataChunks.back().get() + (nextBlockDataOffset * blkSize);
    ++nextBlockDataOffset;
}

void
InfiniteTags::registerBlock(CacheBlk &blk)
{
    blk.registerTagExtractor([this](Addr addr) { return extractTag(addr); });
}

InfiniteTags::TagHashKey
InfiniteTags::makeTagHashKey(const CacheBlk::KeyType &key) const
{
    return std::make_pair(extractTag(key.address), key.secure);
}

CacheBlk *
InfiniteTags::findBlock(const CacheBlk::KeyType &key) const
{
    const auto it = blockMap.find(makeTagHashKey(key));
    if (it == blockMap.end()) {
        return nullptr;
    }

    CacheBlk &blk = *it->second;
    assert(blk.match(key));
    return &blk;
}

ReplaceableEntry *
InfiniteTags::findBlockBySetAndWay(int set, int way) const
{
    if (set != 0 || way < 0) {
        return nullptr;
    }

    for (CacheBlk &blk : const_cast<std::list<CacheBlk> &>(blocks)) {
        if (blk.getWay() == static_cast<uint32_t>(way)) {
            return &blk;
        }
    }

    return nullptr;
}

CacheBlk *
InfiniteTags::findVictim(const CacheBlk::KeyType &key, const std::size_t size,
                         std::vector<CacheBlk *> &evict_blks,
                         const uint64_t partition_id)
{
    if (!invalidBlocks.empty()) {
        const auto victim = invalidBlocks.back();
        invalidBlocks.pop_back();

        if (victim != blocks.begin()) {
            blocks.splice(blocks.begin(), blocks, victim);
        }

        return &blocks.front();
    }

    blocks.emplace_front();
    CacheBlk &blk = blocks.front();
    blk.setPosition(0, blocks.size() - 1);
    registerBlock(blk);
    attachBlockData(blk);
    return &blocks.front();
}

CacheBlk *
InfiniteTags::accessBlock(const PacketPtr pkt, Cycles &lat)
{
    const CacheBlk::KeyType key{pkt->getAddr(), pkt->isSecure()};
    const auto it = blockMap.find(makeTagHashKey(key));

    stats.tagAccesses += 1;
    lat = lookupLatency;

    if (it == blockMap.end()) {
        return nullptr;
    }

    CacheBlk &blk = *it->second;
    assert(blk.match(key));
    stats.dataAccesses += 1;
    blk.increaseRefCount();
    return &blk;
}

Addr
InfiniteTags::extractTag(const Addr addr) const
{
    return blkAlign(addr);
}

void
InfiniteTags::insertBlock(const PacketPtr pkt, CacheBlk *blk)
{
    assert(blk == &blocks.front());

    BaseTags::insertBlock(pkt, blk);
    stats.tagsInUse++;

    [[maybe_unused]] const auto inserted = blockMap.emplace(
        std::make_pair(blk->getTag(), blk->isSecure()), blocks.begin());
    assert(inserted.second);

    if (partitionManager) {
        auto partition_id = partitionManager->readPacketPartitionID(pkt);
        partitionManager->notifyAcquire(partition_id);
    }
}

void
InfiniteTags::invalidate(CacheBlk *blk)
{
    const auto entry =
        blockMap.find(std::make_pair(blk->getTag(), blk->isSecure()));
    assert(entry != blockMap.end());
    const auto block_it = entry->second;
    assert(&*block_it == blk);
    blockMap.erase(entry);

    if (partitionManager) {
        partitionManager->notifyRelease(blk->getPartitionId());
    }

    BaseTags::invalidate(blk);
    stats.tagsInUse--;
    invalidBlocks.push_back(block_it);
}

void
InfiniteTags::moveBlock(CacheBlk *src_blk, CacheBlk *dest_blk)
{
    assert(dest_blk == &blocks.front());

    auto entry =
        blockMap.find(std::make_pair(src_blk->getTag(), src_blk->isSecure()));
    assert(entry != blockMap.end());
    const auto src_it = entry->second;
    assert(&*src_it == src_blk);

    BaseTags::moveBlock(src_blk, dest_blk);

    entry->second = blocks.begin();
    invalidBlocks.push_back(src_it);
}

Addr
InfiniteTags::regenerateBlkAddr(const CacheBlk *blk) const
{
    return blk->getTag();
}

bool
InfiniteTags::anyBlk(std::function<bool(CacheBlk &)> visitor)
{
    for (CacheBlk &blk : blocks) {
        if (visitor(blk)) {
            return true;
        }
    }

    return false;
}

} // namespace gem5
