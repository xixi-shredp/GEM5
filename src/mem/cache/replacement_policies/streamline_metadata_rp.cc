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

#include "mem/cache/replacement_policies/streamline_metadata_rp.hh"

#include <algorithm>
#include <cassert>

#include "base/logging.hh"
#include "params/StreamlineMetadataRP.hh"
#include "sim/cur_tick.hh"

namespace gem5
{

namespace replacement_policy
{

StreamlineMetadata::StreamlineMetadata(const Params &p)
    : Base(p),
      metadataBaseAddr(p.metadata_base_addr),
      metadataStoreAssoc(p.metadata_store_assoc),
      metadataStoreEntries(p.metadata_store_entries),
      maxMetadataSets(metadataStoreAssoc == 0
                          ? 0
                          : metadataStoreEntries / metadataStoreAssoc),
      metadataLineStride(p.metadata_line_stride),
      metadataAddressSpan(MetadataLineBytes * metadataLineStride *
                          (1ULL << PartialTagBits))
{
    fatal_if(
        metadataStoreAssoc == 0,
        "Streamline metadata replacement requires non-zero associativity");
    fatal_if(metadataStoreEntries == 0 ||
                 metadataStoreEntries % metadataStoreAssoc != 0,
             "Streamline metadata entries (%u) must be a non-zero multiple of "
             "associativity (%u)",
             metadataStoreEntries, metadataStoreAssoc);
    fatal_if(
        metadataLineStride < maxMetadataSets,
        "Streamline metadata line stride (%u) must cover metadata sets (%u)",
        metadataLineStride, maxMetadataSets);
}

bool
StreamlineMetadata::isMetadataAddress(Addr address) const
{
    if (address < metadataBaseAddr ||
        (address - metadataBaseAddr) >= metadataAddressSpan) {
        return false;
    }

    const auto lineIndex = (address - metadataBaseAddr) / MetadataLineBytes;
    return (lineIndex % metadataLineStride) < maxMetadataSets;
}

uint64_t
StreamlineMetadata::metadataSet(Addr address) const
{
    panic_if(!isMetadataAddress(address),
             "Address %#x is outside the Streamline metadata region", address);

    const auto lineIndex = (address - metadataBaseAddr) / MetadataLineBytes;
    return lineIndex % metadataLineStride;
}

std::size_t
StreamlineMetadata::chooseVictimIndex(
    const std::vector<bool> &metadata, const std::vector<bool> &valid,
    const std::vector<uint64_t> &etrs,
    const std::vector<uint64_t> &last_touch_ticks)
{
    panic_if(
        metadata.size() != valid.size() || metadata.size() != etrs.size() ||
            metadata.size() != last_touch_ticks.size(),
        "Streamline metadata replacement vectors must have equal lengths");
    panic_if(metadata.empty(), "Streamline replacement needs candidates");

    for (std::size_t i = 0; i < valid.size(); ++i) {
        if (!valid[i]) {
            return i;
        }
    }

    bool foundMetadata = false;
    std::size_t victim = 0;
    for (std::size_t i = 0; i < metadata.size(); ++i) {
        if (!metadata[i]) {
            continue;
        }
        if (!foundMetadata || etrs[i] > etrs[victim] ||
            (etrs[i] == etrs[victim] &&
             last_touch_ticks[i] < last_touch_ticks[victim])) {
            victim = i;
            foundMetadata = true;
        }
    }
    if (foundMetadata) {
        return victim;
    }

    return std::distance(
        last_touch_ticks.begin(),
        std::min_element(last_touch_ticks.begin(), last_touch_ticks.end()));
}

void
StreamlineMetadata::markAccess(
    const std::shared_ptr<ReplacementData> &replacement_data,
    PacketPtr pkt) const
{
    auto data =
        std::static_pointer_cast<StreamlineMetadataReplData>(replacement_data);

    data->valid = true;
    data->lastTouchTick = curTick();
    data->metadata = pkt != nullptr && isMetadataAddress(pkt->getAddr());
    data->etr = data->metadata ? 0 : MaxMetadataEtr;
}

void
StreamlineMetadata::invalidate(
    const std::shared_ptr<ReplacementData> &replacement_data)
{
    auto data =
        std::static_pointer_cast<StreamlineMetadataReplData>(replacement_data);
    data->valid = false;
    data->metadata = false;
    data->etr = MaxMetadataEtr;
    data->lastTouchTick = 0;
}

void
StreamlineMetadata::touch(
    const std::shared_ptr<ReplacementData> &replacement_data,
    const PacketPtr pkt)
{
    markAccess(replacement_data, pkt);
}

void
StreamlineMetadata::touch(
    const std::shared_ptr<ReplacementData> &replacement_data) const
{
    auto data =
        std::static_pointer_cast<StreamlineMetadataReplData>(replacement_data);
    data->lastTouchTick = curTick();
    if (data->metadata) {
        data->etr = 0;
    }
}

void
StreamlineMetadata::reset(
    const std::shared_ptr<ReplacementData> &replacement_data,
    const PacketPtr pkt)
{
    markAccess(replacement_data, pkt);
}

void
StreamlineMetadata::reset(
    const std::shared_ptr<ReplacementData> &replacement_data) const
{
    auto data =
        std::static_pointer_cast<StreamlineMetadataReplData>(replacement_data);
    data->valid = true;
    data->metadata = false;
    data->etr = MaxMetadataEtr;
    data->lastTouchTick = curTick();
}

ReplaceableEntry *
StreamlineMetadata::getVictim(const ReplacementCandidates &candidates) const
{
    assert(!candidates.empty());

    std::vector<bool> metadata;
    std::vector<bool> valid;
    std::vector<uint64_t> etrs;
    std::vector<uint64_t> lastTouchTicks;
    metadata.reserve(candidates.size());
    valid.reserve(candidates.size());
    etrs.reserve(candidates.size());
    lastTouchTicks.reserve(candidates.size());

    for (const auto *candidate : candidates) {
        auto data = std::static_pointer_cast<StreamlineMetadataReplData>(
            candidate->replacementData);
        metadata.push_back(data->metadata);
        valid.push_back(data->valid);
        etrs.push_back(data->etr);
        lastTouchTicks.push_back(data->lastTouchTick);
    }

    const auto victimIdx =
        chooseVictimIndex(metadata, valid, etrs, lastTouchTicks);

    for (std::size_t i = 0; i < candidates.size(); ++i) {
        auto data = std::static_pointer_cast<StreamlineMetadataReplData>(
            candidates[i]->replacementData);
        if (data->valid && data->metadata && i != victimIdx) {
            data->etr = std::min<uint8_t>(MaxMetadataEtr, data->etr + 1);
        }
    }

    return candidates[victimIdx];
}

std::shared_ptr<ReplacementData>
StreamlineMetadata::instantiateEntry()
{
    return std::shared_ptr<ReplacementData>(new StreamlineMetadataReplData());
}

bool
StreamlineMetadata::debugIsMetadataAddress(Addr address) const
{
    return isMetadataAddress(address);
}

uint64_t
StreamlineMetadata::debugMetadataSet(Addr address) const
{
    return metadataSet(address);
}

uint64_t
StreamlineMetadata::debugChooseVictim(
    const std::vector<bool> &metadata, const std::vector<bool> &valid,
    const std::vector<uint64_t> &etrs,
    const std::vector<uint64_t> &last_touch_ticks) const
{
    return chooseVictimIndex(metadata, valid, etrs, last_touch_ticks);
}

} // namespace replacement_policy
} // namespace gem5
