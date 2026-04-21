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

#ifndef __MEM_CACHE_TAGS_PARTITIONING_POLICIES_STREAMLINE_PM_HH__
#define __MEM_CACHE_TAGS_PARTITIONING_POLICIES_STREAMLINE_PM_HH__

#include <string>
#include <unordered_set>

#include "mem/cache/tags/partitioning_policies/partition_manager.hh"
#include "params/StreamlinePartitionManager.hh"

namespace gem5
{

class System;

namespace partitioning_policy
{

class StreamlinePartitionManager : public PartitionManager
{
  public:
    PARAMS(StreamlinePartitionManager);

    explicit StreamlinePartitionManager(
        const StreamlinePartitionManagerParams &p);

    uint64_t readPacketPartitionID(PacketPtr pkt) const override;

  private:
    System *const system;
    const uint64_t metadataPartitionId;
    const std::unordered_set<std::string> metadataRequestorNames;
};

} // namespace partitioning_policy
} // namespace gem5

#endif // __MEM_CACHE_TAGS_PARTITIONING_POLICIES_STREAMLINE_PM_HH__
