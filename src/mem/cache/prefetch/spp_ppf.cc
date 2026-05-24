/*
 * Copyright (c) 2026
 * All rights reserved.
 */

#include "mem/cache/prefetch/spp_ppf.hh"

#include <algorithm>
#include <cassert>
#include <limits>

#include "base/logging.hh"
#include "debug/HWPrefetch.hh"
#include "params/SPP_PPFPrefetcher.hh"

namespace gem5
{

namespace prefetch
{

uint64_t
SPP_PPF::hash(uint64_t key)
{
    key += (key << 12);
    key ^= (key >> 22);
    key += (key << 4);
    key ^= (key >> 9);
    key += (key << 10);
    key ^= (key >> 2);
    key += (key << 7);
    key ^= (key >> 12);
    return (key >> 3) * 2654435761ULL;
}

SPP_PPF::SPP_PPF(const SPP_PPFPrefetcherParams &p)
    : Queued(p),
      signatureShift(p.signature_shift),
      signatureBits(p.signature_bits),
      signatureDeltaBits(p.signature_delta_bits),
      signatureMask(makeMask(p.signature_bits)),
      signatureTagMask(makeMask(p.signature_tag_bits)),
      patternCounterMax(makeMask(p.pattern_counter_bits)),
      globalCounterMax(makeMask(p.global_counter_bits)),
      perceptronWeightMin(-(1 << (p.perceptron_counter_bits - 1))),
      perceptronWeightMax((1 << (p.perceptron_counter_bits - 1)) - 1),
      thresholdHi(p.perceptron_threshold_hi),
      thresholdLo(p.perceptron_threshold_lo),
      positiveUpdateThreshold(p.positive_update_threshold),
      negativeUpdateThreshold(p.negative_update_threshold),
      maxLookaheadDepth(p.max_lookahead_depth),
      maxPrefetchesPerAccess(p.max_prefetches_per_access),
      rejectFilterCandidateWindow(p.reject_filter_candidate_window),
      issueLowConfidence(p.issue_low_confidence),
      signatureTable(p.signature_table_entries),
      patternTable(p.pattern_table_entries,
                   PatternEntry(p.strides_per_pattern_entry)),
      globalHistory(p.global_history_register_entries),
      prefetchFilter(p.prefetch_filter_entries),
      rejectFilter(p.reject_filter_entries),
      perceptronWeights(p.perceptron_entries)
{
    fatal_if(signatureTable.empty(),
             "SPP_PPF requires at least one signature-table entry");
    fatal_if(patternTable.empty(),
             "SPP_PPF requires at least one pattern-table entry");
    fatal_if(globalHistory.empty(), "SPP_PPF requires at least one GHR entry");
    fatal_if(prefetchFilter.empty() || rejectFilter.empty(),
             "SPP_PPF requires non-empty prefetch and reject filters");
    fatal_if(perceptronWeights.empty(),
             "SPP_PPF requires non-empty perceptron weight tables");
    fatal_if(p.strides_per_pattern_entry == 0,
             "SPP_PPF requires at least one stride per pattern entry");
    fatal_if(signatureBits == 0 || signatureBits > 16,
             "SPP_PPF signature_bits must be in the range [1, 16]");
    fatal_if(signatureDeltaBits == 0 || signatureDeltaBits > 15,
             "SPP_PPF signature_delta_bits must be in the range [1, 15]");
    fatal_if(p.pattern_counter_bits == 0 || p.pattern_counter_bits > 16,
             "SPP_PPF pattern_counter_bits must be in the range [1, 16]");
    fatal_if(p.perceptron_counter_bits == 0 || p.perceptron_counter_bits > 8,
             "SPP_PPF perceptron_counter_bits must be in the range [1, 8]");
    fatal_if(p.perceptron_feature_depths.size() != NumFeatures,
             "SPP_PPF requires exactly %u perceptron feature depths",
             NumFeatures);
    fatal_if(
        !p.prefetch_on_access || p.prefetch_on_pf_hit || p.on_miss,
        "SPP_PPF must observe every demand access: set "
        "prefetch_on_access=true, prefetch_on_pf_hit=false, on_miss=false");
    fatal_if(p.use_virtual_addresses,
             "SPP_PPF does not support use_virtual_addresses; its PPF state "
             "and eviction feedback must use physical addresses");
    fatal_if(rejectFilterCandidateWindow == 0,
             "SPP_PPF reject_filter_candidate_window must be non-zero");

    for (unsigned i = 0; i < NumFeatures; ++i) {
        perceptronDepths[i] = p.perceptron_feature_depths[i];
        fatal_if(perceptronDepths[i] == 0 ||
                     perceptronDepths[i] > perceptronWeights.size(),
                 "SPP_PPF feature depth %u is outside the perceptron table",
                 i);
    }

    for (unsigned i = 0; i < signatureTable.size(); ++i) {
        signatureTable[i].lru = i;
    }

    recentPages.resize(p.recent_page_entries, 0);
}

uint64_t
SPP_PPF::makeMask(unsigned bits) const
{
    if (bits >= 64) {
        return std::numeric_limits<uint64_t>::max();
    }
    return (1ULL << bits) - 1;
}

uint64_t
SPP_PPF::signedDelta(stride_t delta) const
{
    return delta < 0 ? -delta + (1ULL << (signatureDeltaBits - 1)) : delta;
}

SPP_PPF::signature_t
SPP_PPF::updateSignature(signature_t signature, stride_t delta) const
{
    uint64_t next = signature;
    next <<= signatureShift;
    next ^= signedDelta(delta);
    return next & signatureMask;
}

unsigned
SPP_PPF::blockOffset(Addr addr) const
{
    return (addr % pageBytes) / blkSize;
}

bool
SPP_PPF::addDeltaToAddress(Addr base, stride_t delta, Addr &result) const
{
    const int64_t base_line = base >> lBlkSize;
    const int64_t target_line = base_line + delta;
    if (target_line < 0) {
        return false;
    }
    result = static_cast<Addr>(target_line) << lBlkSize;
    return true;
}

void
SPP_PPF::updateRecentPages(Addr ppn)
{
    if (recentPages.empty()) {
        return;
    }

    for (int i = recentPages.size() - 1; i > 0; --i) {
        recentPages[i] = recentPages[i - 1];
    }
    recentPages[0] = ppn;
    recentPageCount =
        std::min<unsigned>(recentPageCount + 1, recentPages.size());
}

unsigned
SPP_PPF::distinctRecentPages() const
{
    unsigned distinct = 0;
    for (unsigned i = 0; i < recentPageCount; ++i) {
        unsigned j = 0;
        for (; j < i; ++j) {
            if (recentPages[i] == recentPages[j]) {
                break;
            }
        }
        if (i == j) {
            ++distinct;
        }
    }
    return std::max(1U, distinct);
}

unsigned
SPP_PPF::globalAccuracy() const
{
    return filterIssued == 0 ? 0 : (100 * filterUseful) / filterIssued;
}

void
SPP_PPF::updatePCs(Addr pc)
{
    for (int i = NumRecentPCs - 1; i > 0; --i) {
        recentPCs[i] = recentPCs[i - 1];
    }
    recentPCs[0] = pc;
}

void
SPP_PPF::readAndUpdateSignature(Addr ppn, bool secure, stride_t pageOffset,
                                signature_t &lastSignature,
                                signature_t &currentSignature, stride_t &delta,
                                bool &hit)
{
    const Addr tag = ppn & signatureTagMask;
    unsigned match = signatureTable.size();
    hit = false;
    lastSignature = 0;
    currentSignature = 0;
    delta = 0;

    for (unsigned i = 0; i < signatureTable.size(); ++i) {
        auto &entry = signatureTable[i];
        if (entry.valid && entry.secure == secure && entry.tag == tag) {
            match = i;
            hit = true;
            break;
        }
    }

    if (hit) {
        auto &entry = signatureTable[match];
        delta = pageOffset - entry.lastOffset;
        if (delta == 0) {
            currentSignature = entry.signature;
        } else {
            lastSignature = entry.signature;
            entry.signature = updateSignature(entry.signature, delta);
            currentSignature = entry.signature;
            entry.lastOffset = pageOffset;
        }
    } else {
        for (unsigned i = 0; i < signatureTable.size(); ++i) {
            if (!signatureTable[i].valid) {
                match = i;
                break;
            }
        }

        if (match == signatureTable.size()) {
            unsigned max_lru = 0;
            for (unsigned i = 0; i < signatureTable.size(); ++i) {
                if (signatureTable[i].lru >= max_lru) {
                    max_lru = signatureTable[i].lru;
                    match = i;
                }
            }
        }

        auto &entry = signatureTable[match];
        entry.valid = true;
        entry.secure = secure;
        entry.tag = tag;
        entry.signature = 0;
        entry.lastOffset = pageOffset;
        currentSignature = 0;

        GhrEntry ghr_entry;
        if (checkGhr(pageOffset, ghr_entry)) {
            entry.signature =
                updateSignature(ghr_entry.signature, ghr_entry.delta);
            currentSignature = entry.signature;
        }
    }

    const unsigned old_lru = signatureTable[match].lru;
    for (auto &entry : signatureTable) {
        if (entry.lru < old_lru) {
            ++entry.lru;
        }
    }
    signatureTable[match].lru = 0;
}

void
SPP_PPF::updatePattern(signature_t signature, stride_t delta)
{
    if (delta == 0) {
        return;
    }

    PatternEntry &entry = getPattern(signature);
    for (auto &slot : entry.strides) {
        if (slot.delta == delta) {
            ++slot.counter;
            ++entry.counter;
            if (entry.counter > patternCounterMax) {
                for (auto &decay : entry.strides) {
                    decay.counter >>= 1;
                }
                entry.counter >>= 1;
            }
            return;
        }
    }

    auto victim = entry.strides.begin();
    for (auto it = entry.strides.begin(); it != entry.strides.end(); ++it) {
        if (it->counter < victim->counter) {
            victim = it;
        }
    }

    victim->delta = delta;
    victim->counter = 0;
    ++entry.counter;
    if (entry.counter > patternCounterMax) {
        for (auto &decay : entry.strides) {
            decay.counter >>= 1;
        }
        entry.counter >>= 1;
    }
}

SPP_PPF::PatternEntry &
SPP_PPF::getPattern(signature_t signature)
{
    return patternTable[hash(signature) % patternTable.size()];
}

void
SPP_PPF::updateGhr(signature_t signature, unsigned confidence, stride_t offset,
                   stride_t delta)
{
    unsigned victim = globalHistory.size();
    unsigned min_confidence = std::numeric_limits<unsigned>::max();

    for (unsigned i = 0; i < globalHistory.size(); ++i) {
        auto &entry = globalHistory[i];
        if (entry.valid && entry.offset == offset) {
            entry.signature = signature;
            entry.confidence = confidence;
            entry.delta = delta;
            return;
        }
        if (!entry.valid) {
            victim = i;
            break;
        }
        if (entry.confidence < min_confidence) {
            min_confidence = entry.confidence;
            victim = i;
        }
    }

    auto &entry = globalHistory[victim];
    entry.valid = true;
    entry.signature = signature;
    entry.confidence = confidence;
    entry.offset = offset;
    entry.delta = delta;
}

bool
SPP_PPF::checkGhr(stride_t offset, GhrEntry &entry) const
{
    bool found = false;
    unsigned max_confidence = 0;
    for (const auto &candidate : globalHistory) {
        if (candidate.valid && candidate.offset == offset &&
            candidate.confidence >= max_confidence) {
            max_confidence = candidate.confidence;
            entry = candidate;
            found = true;
        }
    }
    return found;
}

void
SPP_PPF::getPerceptronIndices(const FeatureRecord &record,
                              std::array<unsigned, NumFeatures> &indices) const
{
    const Addr cache_line = record.address >> lBlkSize;
    const Addr page_addr = record.address / pageBytes;
    const uint64_t sig_delta = signedDelta(record.delta);

    std::array<uint64_t, NumFeatures> features = {
        record.address,
        cache_line,
        page_addr,
        record.confidence ^ page_addr,
        record.currentSignature ^ sig_delta,
        record.pc1 ^ (record.pc2 >> 1) ^ (record.pc3 >> 2),
        record.pc ^ record.depth,
        record.pc ^ sig_delta,
        record.confidence};

    for (unsigned i = 0; i < NumFeatures; ++i) {
        indices[i] = features[i] % perceptronDepths[i];
    }
}

int
SPP_PPF::predict(const FeatureRecord &record) const
{
    std::array<unsigned, NumFeatures> indices;
    getPerceptronIndices(record, indices);

    int sum = 0;
    for (unsigned i = 0; i < NumFeatures; ++i) {
        sum += perceptronWeights[indices[i]][i];
    }
    return sum;
}

void
SPP_PPF::updateWeights(const FeatureRecord &record, bool useful)
{
    if ((useful && record.sum >= positiveUpdateThreshold) ||
        (!useful && record.sum <= negativeUpdateThreshold)) {
        return;
    }

    std::array<unsigned, NumFeatures> indices;
    getPerceptronIndices(record, indices);

    const int delta = useful ? 1 : -1;
    for (unsigned i = 0; i < NumFeatures; ++i) {
        int weight = perceptronWeights[indices[i]][i] + delta;
        weight = std::clamp(weight, perceptronWeightMin, perceptronWeightMax);
        perceptronWeights[indices[i]][i] = weight;
    }
}

uint64_t
SPP_PPF::filterHash(Addr addr, bool secure) const
{
    const Addr line = blockIndex(addr);
    return hash(line ^ (secure ? 0x9e3779b97f4a7c15ULL : 0));
}

unsigned
SPP_PPF::filterIndex(uint64_t hash, unsigned remainderBits,
                     unsigned entries) const
{
    return (hash >> remainderBits) % entries;
}

uint64_t
SPP_PPF::filterRemainder(uint64_t hash, unsigned remainderBits) const
{
    return hash & makeMask(remainderBits);
}

bool
SPP_PPF::filterMatch(const FilterEntry &entry, uint64_t remainder,
                     bool secure) const
{
    if (!entry.valid && !entry.useful) {
        return false;
    }

    return entry.secure == secure && entry.remainder == remainder;
}

bool
SPP_PPF::inPrefetchQueue(Addr addr, bool secure) const
{
    const Addr blk_addr = blockAddress(addr);
    for (const auto &entry : pfq) {
        if (blockAddress(entry.pfInfo.getAddr()) == blk_addr &&
            entry.pfInfo.isSecure() == secure) {
            return true;
        }
    }
    for (const auto &entry : pfqMissingTranslation) {
        if (blockAddress(entry.pfInfo.getAddr()) == blk_addr &&
            entry.pfInfo.isSecure() == secure) {
            return true;
        }
    }
    return false;
}

bool
SPP_PPF::acceptedFilterContains(Addr addr, bool secure) const
{
    const uint64_t h = filterHash(addr, secure);
    const unsigned index = filterIndex(h, 6, prefetchFilter.size());
    return filterMatch(prefetchFilter[index], filterRemainder(h, 6), secure);
}

bool
SPP_PPF::pendingAcceptedContains(Addr addr, bool secure) const
{
    const Addr blk_addr = blockAddress(addr);
    return std::any_of(
        pendingAcceptedRecords.begin(), pendingAcceptedRecords.end(),
        [blk_addr, secure](const auto &entry) {
            return entry.addr == blk_addr && entry.secure == secure;
        });
}

void
SPP_PPF::addPendingAccepted(Addr addr, bool secure,
                            const FeatureRecord &record)
{
    if (acceptedFilterContains(addr, secure) ||
        pendingAcceptedContains(addr, secure)) {
        return;
    }

    PendingAcceptedRecord pending;
    pending.addr = blockAddress(addr);
    pending.secure = secure;
    pending.record = record;
    pendingAcceptedRecords.push_back(pending);
}

bool
SPP_PPF::recordAccepted(Addr addr, bool secure, const FeatureRecord &record)
{
    const uint64_t h = filterHash(addr, secure);
    const unsigned index = filterIndex(h, 6, prefetchFilter.size());
    const unsigned reject_index = filterIndex(h, 8, rejectFilter.size());
    const uint64_t remainder = filterRemainder(h, 6);

    if (filterMatch(prefetchFilter[index], remainder, secure)) {
        return false;
    }

    auto &entry = prefetchFilter[index];
    entry.valid = true;
    entry.useful = false;
    entry.secure = secure;
    entry.remainder = remainder;
    entry.record = record;

    if (filterMatch(rejectFilter[reject_index], filterRemainder(h, 8),
                    secure)) {
        rejectFilter[reject_index].valid = false;
        rejectFilter[reject_index].useful = false;
    }

    ++filterIssued;
    if (filterIssued > globalCounterMax) {
        filterIssued >>= 1;
        filterUseful >>= 1;
    }

    return true;
}

void
SPP_PPF::clearAccepted(Addr addr, bool secure)
{
    addr = blockAddress(addr);
    const uint64_t h = filterHash(addr, secure);
    const unsigned index = filterIndex(h, 6, prefetchFilter.size());
    const uint64_t remainder = filterRemainder(h, 6);

    auto &entry = prefetchFilter[index];
    if (!filterMatch(entry, remainder, secure)) {
        return;
    }

    if (entry.valid && filterIssued > 0) {
        --filterIssued;
    }
    if (entry.useful && filterUseful > 0) {
        --filterUseful;
    }
    entry.valid = false;
    entry.useful = false;
}

bool
SPP_PPF::recordRejected(Addr addr, bool secure, const FeatureRecord &record)
{
    const uint64_t h = filterHash(addr, secure);
    const unsigned reject_index = filterIndex(h, 8, rejectFilter.size());

    if (acceptedFilterContains(addr, secure)) {
        return false;
    }

    auto &entry = rejectFilter[reject_index];
    entry.valid = true;
    entry.useful = false;
    entry.secure = secure;
    entry.remainder = filterRemainder(h, 8);
    entry.record = record;
    return true;
}

void
SPP_PPF::trainDemand(Addr addr, bool secure)
{
    addr = blockAddress(addr);
    const uint64_t h = filterHash(addr, secure);
    const unsigned accept_index = filterIndex(h, 6, prefetchFilter.size());
    const unsigned reject_index = filterIndex(h, 8, rejectFilter.size());
    const uint64_t accept_remainder = filterRemainder(h, 6);

    auto &accepted = prefetchFilter[accept_index];
    const bool accepted_match =
        filterMatch(accepted, accept_remainder, secure);
    if (accepted_match && !accepted.useful) {
        accepted.useful = true;
        if (accepted.valid) {
            ++filterUseful;
            updateWeights(accepted.record, true);
        }
        return;
    }

    auto &rejected = rejectFilter[reject_index];
    if (!accepted_match &&
        filterMatch(rejected, filterRemainder(h, 8), secure)) {
        updateWeights(rejected.record, true);
        rejected.valid = false;
        rejected.useful = false;
    }
}

void
SPP_PPF::trainEviction(Addr addr, bool secure)
{
    addr = blockAddress(addr);
    const uint64_t h = filterHash(addr, secure);
    const unsigned accept_index = filterIndex(h, 6, prefetchFilter.size());
    const unsigned reject_index = filterIndex(h, 8, rejectFilter.size());

    auto &accepted = prefetchFilter[accept_index];
    if (filterMatch(accepted, filterRemainder(h, 6), secure) &&
        accepted.valid && !accepted.useful) {
        if (filterUseful > 0) {
            --filterUseful;
        }
        updateWeights(accepted.record, false);
    }

    accepted.valid = false;
    accepted.useful = false;
    rejectFilter[reject_index].valid = false;
    rejectFilter[reject_index].useful = false;
}

void
SPP_PPF::notifyEvict(const CacheDataUpdateProbeArg &info)
{
    // Skip-fill LLC candidates are returned through tempBlock, which is not
    // cache-resident and must not consume accepted/rejected PPF feedback.
    if (!info.accessor.inCache(info.addr, info.isSecure)) {
        return;
    }

    trainEviction(info.addr, info.isSecure);
}

void
SPP_PPF::notify(const CacheAccessProbeArg &acc, const PrefetchInfo &pfi)
{
    if (acc.pkt->cmd.isHWPrefetch()) {
        return;
    }

    globalAccuracySnapshot = globalAccuracy();
    useGlobalAccuracySnapshot = true;
    pendingAcceptedRecords.clear();
    squashQueuedPrefetches(blockAddress(pfi.getAddr()), pfi.isSecure());
    trainDemand(pfi.getAddr(), pfi.isSecure());
    Queued::notify(acc, pfi);
    pendingAcceptedRecords.clear();
    useGlobalAccuracySnapshot = false;
}

void
SPP_PPF::prefetchQueued(const PrefetchInfo &pfi, const AddrPriority &addr_prio)
{
    if (addr_prio.skipCacheFill || addr_prio.second <= 0) {
        return;
    }

    const Addr addr = blockAddress(pfi.getAddr());
    const bool secure = pfi.isSecure();
    auto it = std::find_if(
        pendingAcceptedRecords.begin(), pendingAcceptedRecords.end(),
        [addr, secure](const auto &entry) {
            return entry.addr == addr && entry.secure == secure;
        });

    if (it == pendingAcceptedRecords.end()) {
        return;
    }

    recordAccepted(addr, secure, it->record);
    pendingAcceptedRecords.erase(it);
}

void
SPP_PPF::prefetchSquashed(const PrefetchInfo &pfi,
                          const AddrPriority &addr_prio)
{
    if (addr_prio.skipCacheFill || addr_prio.second <= 0) {
        return;
    }

    clearAccepted(pfi.getAddr(), pfi.isSecure());
}

void
SPP_PPF::calculatePrefetch(const PrefetchInfo &pfi,
                           std::vector<AddrPriority> &addresses,
                           const CacheAccessor &cache)
{
    const Addr trigger_addr = pfi.getAddr();
    const Addr request_addr = blockAddress(trigger_addr);
    const Addr ppn = request_addr / pageBytes;
    const bool secure = pfi.isSecure();
    const stride_t page_offset = blockOffset(request_addr);
    const Addr pc = pfi.hasPC() ? pfi.getPC() : 0;

    updateRecentPages(ppn);

    signature_t last_signature = 0;
    signature_t current_signature = 0;
    stride_t delta = 0;
    bool signature_hit = false;
    readAndUpdateSignature(ppn, secure, page_offset, last_signature,
                           current_signature, delta, signature_hit);

    if (signature_hit && delta == 0) {
        updatePCs(pc);
        return;
    }

    if (last_signature != 0) {
        updatePattern(last_signature, delta);
    }

    updatePCs(pc);

    Addr base_addr = request_addr;
    stride_t accumulated_delta = 0;
    unsigned lookahead_confidence = 100;
    unsigned depth = 0;
    unsigned num_page_prefetches = 0;
    unsigned num_candidates = 0;
    const unsigned access_global_accuracy =
        useGlobalAccuracySnapshot ? globalAccuracySnapshot : globalAccuracy();
    const unsigned page_budget = std::max(
        1U, (queueSize + distinctRecentPages() - 1) / distinctRecentPages());

    while (depth < maxLookaheadDepth &&
           num_candidates < maxPrefetchesPerAccess) {
        PatternEntry &pattern = getPattern(current_signature);
        if (pattern.counter == 0) {
            break;
        }

        unsigned max_confidence = 0;
        stride_t lookahead_delta = 0;
        bool found_candidate = false;

        for (const auto &slot : pattern.strides) {
            if (slot.delta == 0 || slot.counter == 0) {
                continue;
            }

            const unsigned local_confidence =
                (100 * slot.counter) / pattern.counter;
            const unsigned prefetch_confidence =
                depth == 0 ? local_confidence
                           : (access_global_accuracy * slot.counter /
                              pattern.counter * lookahead_confidence / 100);

            if (prefetch_confidence == 0) {
                continue;
            }

            Addr pf_addr = 0;
            if (!addDeltaToAddress(base_addr, slot.delta, pf_addr)) {
                continue;
            }

            FeatureRecord record;
            record.address = trigger_addr;
            record.pc = pc;
            record.pc1 = recentPCs[1];
            record.pc2 = recentPCs[2];
            record.pc3 = recentPCs[3];
            record.delta = accumulated_delta + slot.delta;
            record.lastSignature = last_signature;
            record.currentSignature = current_signature;
            record.confidence = prefetch_confidence;
            record.depth = depth;
            record.sum = predict(record);

            const bool high_confidence = record.sum >= thresholdHi;
            const bool ppf_candidate = record.sum >= thresholdLo;

            const bool in_cache = cache.inCache(pf_addr, secure);
            const bool in_miss_queue = cache.inMissQueue(pf_addr, secure);
            const bool miss_allocates_on_fill =
                in_miss_queue &&
                cache.missQueueAllocatesOnFill(pf_addr, secure);
            const bool miss_queue_skip_fill_prefetch =
                in_miss_queue &&
                cache.missQueueIsSkipFillPrefetch(pf_addr, secure);
            const bool in_prefetch_queue = inPrefetchQueue(pf_addr, secure);
            const bool prefetch_queue_full =
                pfq.size() + addresses.size() >= queueSize;
            if (high_confidence && prefetch_queue_full && !in_prefetch_queue &&
                !in_miss_queue) {
                continue;
            }

            if (ppf_candidate) {
                if (num_candidates >= maxPrefetchesPerAccess) {
                    break;
                }
                ++num_candidates;
            }

            const bool low_or_llc_candidate = record.sum < thresholdHi;
            bool filter_allows_prefetch = true;
            if (samePage(request_addr, pf_addr) && low_or_llc_candidate) {
                if (num_candidates < rejectFilterCandidateWindow) {
                    filter_allows_prefetch =
                        recordRejected(pf_addr, secure, record);
                } else if (ppf_candidate) {
                    filter_allows_prefetch =
                        !acceptedFilterContains(pf_addr, secure);
                }
            }

            if (!ppf_candidate) {
                continue;
            }

            found_candidate = true;
            if (prefetch_confidence > max_confidence) {
                max_confidence = prefetch_confidence;
                lookahead_delta = slot.delta;
            }

            if (!samePage(request_addr, pf_addr)) {
                updateGhr(current_signature, prefetch_confidence,
                          blockOffset(pf_addr), slot.delta);
                continue;
            }

            if (num_page_prefetches >= page_budget ||
                (prefetch_queue_full && !in_prefetch_queue &&
                 !in_miss_queue) ||
                in_cache ||
                (in_miss_queue &&
                 (!high_confidence || miss_allocates_on_fill ||
                  !miss_queue_skip_fill_prefetch)) ||
                (in_prefetch_queue && !high_confidence)) {
                continue;
            }

            if (!high_confidence) {
                if (!filter_allows_prefetch) {
                    continue;
                }
                addresses.emplace_back(pf_addr, 0, !issueLowConfidence);
                ++num_page_prefetches;
                DPRINTF(HWPrefetch,
                        "SPP_PPF LLC-confidence candidate %#x delta %d "
                        "sum %d conf %u skip_fill %d\n",
                        pf_addr, slot.delta, record.sum, prefetch_confidence,
                        !issueLowConfidence);
            } else {
                if (!acceptedFilterContains(pf_addr, secure) &&
                    !pendingAcceptedContains(pf_addr, secure)) {
                    addPendingAccepted(pf_addr, secure, record);
                    addresses.emplace_back(pf_addr, 1);
                    ++num_page_prefetches;
                    DPRINTF(HWPrefetch,
                            "SPP_PPF candidate %#x delta %d sum %d conf %u\n",
                            pf_addr, slot.delta, record.sum,
                            prefetch_confidence);
                }
            }

            if (addresses.size() >= maxPrefetchesPerAccess) {
                break;
            }
        }

        if (!found_candidate || max_confidence == 0) {
            break;
        }

        Addr next_base = 0;
        if (!addDeltaToAddress(base_addr, lookahead_delta, next_base)) {
            break;
        }
        base_addr = next_base;
        accumulated_delta += lookahead_delta;
        current_signature =
            updateSignature(current_signature, lookahead_delta);
        lookahead_confidence = max_confidence;
        ++depth;
    }
}

} // namespace prefetch
} // namespace gem5
