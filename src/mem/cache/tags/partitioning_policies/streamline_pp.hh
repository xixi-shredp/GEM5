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

#ifndef __MEM_CACHE_TAGS_PARTITIONING_POLICIES_STREAMLINE_PP_HH__
#define __MEM_CACHE_TAGS_PARTITIONING_POLICIES_STREAMLINE_PP_HH__

#include <vector>

#include "mem/cache/tags/partitioning_policies/base_pp.hh"
#include "params/StreamlinePartitioningPolicy.hh"

namespace gem5
{

class ReplaceableEntry;

namespace partitioning_policy
{

class StreamlinePartitioningPolicy : public BasePartitioningPolicy
{
  public:
    PARAMS(StreamlinePartitioningPolicy);

    explicit StreamlinePartitioningPolicy(
        const StreamlinePartitioningPolicyParams &params);

    void filterByPartition(std::vector<ReplaceableEntry *> &entries,
                           const uint64_t partition_id) const override;

    void
    notifyAcquire(const uint64_t partition_id) override
    {}
    void
    notifyRelease(const uint64_t partition_id) override
    {}

    void setPartitionLevel(uint64_t level);
    uint64_t
    getPartitionLevel() const
    {
        return partitionLevel;
    }

    uint64_t debugActiveMetadataSetCount(uint64_t partition_level) const;
    bool debugAllowsEntry(uint64_t partition_id, uint64_t cache_set,
                          uint64_t cache_way, uint64_t partition_level) const;

  private:
    const unsigned cacheAssociativity;
    const unsigned metadataAssociativity;
    const uint64_t maxMetadataSets;
    const uint64_t sampleMetadataSets;
    uint64_t partitionLevel;

    uint64_t activeMetadataSetCount(uint64_t level) const;
    bool isActiveMetadataSet(uint64_t cache_set, uint64_t level) const;
    bool isMetadataWay(uint64_t way) const;
    bool allowsEntry(uint64_t partition_id, uint64_t cache_set,
                     uint64_t cache_way, uint64_t partition_level) const;
};

} // namespace partitioning_policy
} // namespace gem5

#endif // __MEM_CACHE_TAGS_PARTITIONING_POLICIES_STREAMLINE_PP_HH__
