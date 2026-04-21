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

#ifndef __MEM_CACHE_REPLACEMENT_POLICIES_STREAMLINE_METADATA_RP_HH__
#define __MEM_CACHE_REPLACEMENT_POLICIES_STREAMLINE_METADATA_RP_HH__

#include <memory>
#include <vector>

#include "base/types.hh"
#include "mem/cache/replacement_policies/base.hh"
#include "params/StreamlineMetadataRP.hh"

namespace gem5
{

namespace replacement_policy
{

class StreamlineMetadata : public Base
{
  private:
    static constexpr Addr MetadataLineBytes = 64;
    static constexpr unsigned PartialTagBits = 6;
    static constexpr uint8_t MaxMetadataEtr = 7;

    struct StreamlineMetadataReplData : ReplacementData
    {
        bool valid = false;
        bool metadata = false;
        uint8_t etr = MaxMetadataEtr;
        Tick lastTouchTick = 0;
    };

    const Addr metadataBaseAddr;
    const uint64_t metadataStoreAssoc;
    const uint64_t metadataStoreEntries;
    const uint64_t maxMetadataSets;
    const uint64_t metadataLineStride;
    const Addr metadataAddressSpan;

    bool isMetadataAddress(Addr address) const;
    uint64_t metadataSet(Addr address) const;
    static std::size_t
    chooseVictimIndex(const std::vector<bool> &metadata,
                      const std::vector<bool> &valid,
                      const std::vector<uint64_t> &etrs,
                      const std::vector<uint64_t> &last_touch_ticks);
    void markAccess(const std::shared_ptr<ReplacementData> &replacement_data,
                    PacketPtr pkt) const;

  public:
    typedef StreamlineMetadataRPParams Params;

    explicit StreamlineMetadata(const Params &p);
    ~StreamlineMetadata() = default;

    void invalidate(
        const std::shared_ptr<ReplacementData> &replacement_data) override;
    void touch(const std::shared_ptr<ReplacementData> &replacement_data,
               const PacketPtr pkt) override;
    void touch(const std::shared_ptr<ReplacementData> &replacement_data)
        const override;
    void reset(const std::shared_ptr<ReplacementData> &replacement_data,
               const PacketPtr pkt) override;
    void reset(const std::shared_ptr<ReplacementData> &replacement_data)
        const override;
    ReplaceableEntry *
    getVictim(const ReplacementCandidates &candidates) const override;
    std::shared_ptr<ReplacementData> instantiateEntry() override;

    bool debugIsMetadataAddress(Addr address) const;
    uint64_t debugMetadataSet(Addr address) const;
    uint64_t
    debugChooseVictim(const std::vector<bool> &metadata,
                      const std::vector<bool> &valid,
                      const std::vector<uint64_t> &etrs,
                      const std::vector<uint64_t> &last_touch_ticks) const;
};

} // namespace replacement_policy
} // namespace gem5

#endif // __MEM_CACHE_REPLACEMENT_POLICIES_STREAMLINE_METADATA_RP_HH__
