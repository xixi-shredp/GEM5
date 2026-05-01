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

#include <cassert>
#include <cstdint>

#include "mem/cache/tags/partitioning_policies/partition_manager.hh"

namespace gem5
{

InfiniteTags::InfiniteTags(const Params &p) : BaseTags(p)
{}

void
InfiniteTags::tagsInit()
{}

void
InfiniteTags::attachBlockData(CacheBlk &blk)
{
    blockData.emplace_back(new uint8_t[blkSize]);
    blk.data = blockData.back().get();
}

void
InfiniteTags::registerBlock(CacheBlk &blk)
{
    blk.registerTagExtractor([this](Addr addr) { return extractTag(addr); });
}

CacheBlk *
InfiniteTags::findBlock(const CacheBlk::KeyType &key) const
{
    for (CacheBlk &blk : const_cast<std::list<CacheBlk> &>(blocks)) {
        if (blk.match(key)) {
            return &blk;
        }
    }

    return nullptr;
}

ReplaceableEntry *
InfiniteTags::findBlockBySetAndWay(int set, int way) const
{
    if (set != 0 || way < 0) {
        return nullptr;
    }

    int current_way = 0;
    for (CacheBlk &blk : const_cast<std::list<CacheBlk> &>(blocks)) {
        if (current_way == way) {
            return &blk;
        }
        ++current_way;
    }

    return nullptr;
}

CacheBlk *
InfiniteTags::findVictim(const CacheBlk::KeyType &key, const std::size_t size,
                         std::vector<CacheBlk *> &evict_blks,
                         const uint64_t partition_id)
{
    for (CacheBlk &blk : blocks) {
        if (!blk.isValid()) {
            return &blk;
        }
    }

    blocks.emplace_back();
    CacheBlk &blk = blocks.back();
    blk.setPosition(0, blocks.size() - 1);
    registerBlock(blk);
    attachBlockData(blk);
    return &blocks.back();
}

CacheBlk *
InfiniteTags::accessBlock(const PacketPtr pkt, Cycles &lat)
{
    CacheBlk *blk = findBlock({pkt->getAddr(), pkt->isSecure()});

    stats.tagAccesses += blocks.size();
    if (blk != nullptr) {
        stats.dataAccesses += 1;
        blk->increaseRefCount();
    }

    lat = lookupLatency;
    return blk;
}

Addr
InfiniteTags::extractTag(const Addr addr) const
{
    return blkAlign(addr);
}

void
InfiniteTags::insertBlock(const PacketPtr pkt, CacheBlk *blk)
{
    BaseTags::insertBlock(pkt, blk);
    stats.tagsInUse++;

    if (partitionManager) {
        auto partition_id = partitionManager->readPacketPartitionID(pkt);
        partitionManager->notifyAcquire(partition_id);
    }
}

void
InfiniteTags::invalidate(CacheBlk *blk)
{
    if (partitionManager) {
        partitionManager->notifyRelease(blk->getPartitionId());
    }

    BaseTags::invalidate(blk);
    stats.tagsInUse--;
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
