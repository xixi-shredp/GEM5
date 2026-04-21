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

#include "mem/cache/tags/partitioning_policies/streamline_pp.hh"

#include <algorithm>

#include "base/logging.hh"
#include "mem/cache/replacement_policies/replaceable_entry.hh"

namespace gem5
{

namespace partitioning_policy
{

StreamlinePartitioningPolicy::StreamlinePartitioningPolicy(
    const StreamlinePartitioningPolicyParams &params)
    : BasePartitioningPolicy(params),
      cacheAssociativity(params.cache_associativity),
      metadataAssociativity(params.metadata_associativity),
      maxMetadataSets(params.max_metadata_sets),
      sampleMetadataSets(params.sample_metadata_sets),
      partitionLevel(params.initial_partition_level)
{
    fatal_if(cacheAssociativity == 0, "Cache associativity must be non-zero");
    fatal_if(metadataAssociativity == 0 ||
                 metadataAssociativity > cacheAssociativity,
             "Streamline metadata associativity must be in range [1, cache "
             "associativity]");
    fatal_if(maxMetadataSets == 0,
             "Streamline metadata set count must be non-zero");
    setPartitionLevel(partitionLevel);
}

uint64_t
StreamlinePartitioningPolicy::activeMetadataSetCount(uint64_t level) const
{
    switch (level) {
        case 0:
            return std::min(sampleMetadataSets, maxMetadataSets);
        case 1:
            return maxMetadataSets / 2;
        case 2:
            return maxMetadataSets;
        default:
            panic("Unsupported Streamline partition level %u", level);
    }
}

bool
StreamlinePartitioningPolicy::isActiveMetadataSet(uint64_t cache_set,
                                                  uint64_t level) const
{
    if (cache_set >= maxMetadataSets) {
        return false;
    }

    const auto activeSetCount = activeMetadataSetCount(level);
    fatal_if(activeSetCount == 0 || (maxMetadataSets % activeSetCount) != 0,
             "Streamline metadata set geometry requires max sets (%u) to be "
             "divisible by active set count (%u)",
             maxMetadataSets, activeSetCount);

    const auto activeSetStride = maxMetadataSets / activeSetCount;
    return (cache_set % activeSetStride) == 0;
}

bool
StreamlinePartitioningPolicy::isMetadataWay(uint64_t way) const
{
    panic_if(way >= cacheAssociativity,
             "Cache way %u exceeds cache associativity %u", way,
             cacheAssociativity);
    return way >= (cacheAssociativity - metadataAssociativity);
}

bool
StreamlinePartitioningPolicy::allowsEntry(uint64_t partition_id,
                                          uint64_t cache_set,
                                          uint64_t cache_way,
                                          uint64_t partition_level) const
{
    const bool activeMetadataSet =
        isActiveMetadataSet(cache_set, partition_level);
    const bool metadataWay = isMetadataWay(cache_way);

    if (partition_id == 1) {
        return activeMetadataSet && metadataWay;
    }

    if (!activeMetadataSet) {
        return true;
    }

    return !metadataWay;
}

void
StreamlinePartitioningPolicy::filterByPartition(
    std::vector<ReplaceableEntry *> &entries,
    const uint64_t partition_id) const
{
    const auto new_end = std::remove_if(
        entries.begin(), entries.end(),
        [this, partition_id](ReplaceableEntry *entry) {
            return !allowsEntry(partition_id, entry->getSet(), entry->getWay(),
                                partitionLevel);
        });
    entries.erase(new_end, entries.end());
}

void
StreamlinePartitioningPolicy::setPartitionLevel(uint64_t level)
{
    panic_if(level > 2, "Unsupported Streamline partition level %u", level);
    partitionLevel = level;
}

uint64_t
StreamlinePartitioningPolicy::debugActiveMetadataSetCount(
    uint64_t partition_level) const
{
    return activeMetadataSetCount(partition_level);
}

bool
StreamlinePartitioningPolicy::debugAllowsEntry(uint64_t partition_id,
                                               uint64_t cache_set,
                                               uint64_t cache_way,
                                               uint64_t partition_level) const
{
    return allowsEntry(partition_id, cache_set, cache_way, partition_level);
}

} // namespace partitioning_policy
} // namespace gem5
