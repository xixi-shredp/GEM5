/*
 * Copyright (c) 2026
 * All rights reserved.
 *
 * This implementation is based on the DPC-3 SPP+PPF submission described in
 * "Enhancing Signature Path Prefetching with Perceptron Prefetch Filtering".
 */

#ifndef __MEM_CACHE_PREFETCH_SPP_PPF_HH__
#define __MEM_CACHE_PREFETCH_SPP_PPF_HH__

#include <array>
#include <cstdint>
#include <vector>

#include "base/types.hh"
#include "mem/cache/prefetch/queued.hh"

namespace gem5
{

struct SPP_PPFPrefetcherParams;

namespace prefetch
{

class SPP_PPF : public Queued
{
  private:
    static constexpr unsigned NumFeatures = 9;
    static constexpr unsigned NumRecentPCs = 4;

    using signature_t = uint16_t;
    using stride_t = int16_t;

    struct SignatureEntry
    {
        bool valid = false;
        bool secure = false;
        Addr tag = 0;
        stride_t lastOffset = 0;
        signature_t signature = 0;
        unsigned lru = 0;
    };

    struct PatternStrideEntry
    {
        stride_t delta = 0;
        unsigned counter = 0;
    };

    struct PatternEntry
    {
        std::vector<PatternStrideEntry> strides;
        unsigned counter = 0;

        PatternEntry() = default;
        explicit PatternEntry(unsigned numStrides) : strides(numStrides) {}
    };

    struct GhrEntry
    {
        bool valid = false;
        signature_t signature = 0;
        unsigned confidence = 0;
        stride_t offset = 0;
        stride_t delta = 0;
    };

    struct FeatureRecord
    {
        Addr address = 0;
        Addr pc = 0;
        Addr pc1 = 0;
        Addr pc2 = 0;
        Addr pc3 = 0;
        stride_t delta = 0;
        signature_t lastSignature = 0;
        signature_t currentSignature = 0;
        unsigned confidence = 0;
        unsigned depth = 0;
        int sum = 0;
    };

    struct FilterEntry
    {
        bool valid = false;
        bool useful = false;
        bool secure = false;
        uint64_t remainder = 0;
        FeatureRecord record;
    };

    struct PendingAcceptedRecord
    {
        Addr addr = 0;
        bool secure = false;
        FeatureRecord record;
    };

    const unsigned signatureShift;
    const unsigned signatureBits;
    const unsigned signatureDeltaBits;
    const uint64_t signatureMask;
    const uint64_t signatureTagMask;
    const unsigned patternCounterMax;
    const unsigned globalCounterMax;
    const int perceptronWeightMin;
    const int perceptronWeightMax;
    const int thresholdHi;
    const int thresholdLo;
    const int positiveUpdateThreshold;
    const int negativeUpdateThreshold;
    const unsigned maxLookaheadDepth;
    const unsigned maxPrefetchesPerAccess;
    const unsigned rejectFilterCandidateWindow;
    const bool issueLowConfidence;

    std::vector<SignatureEntry> signatureTable;
    std::vector<PatternEntry> patternTable;
    std::vector<GhrEntry> globalHistory;
    std::vector<FilterEntry> prefetchFilter;
    std::vector<FilterEntry> rejectFilter;
    std::vector<std::array<int8_t, NumFeatures>> perceptronWeights;
    std::array<unsigned, NumFeatures> perceptronDepths;
    std::array<Addr, NumRecentPCs> recentPCs = {};
    std::vector<Addr> recentPages;
    std::vector<PendingAcceptedRecord> pendingAcceptedRecords;
    unsigned recentPageCount = 0;
    bool useGlobalAccuracySnapshot = false;
    unsigned globalAccuracySnapshot = 0;
    uint64_t filterUseful = 0;
    uint64_t filterIssued = 0;

    static uint64_t hash(uint64_t key);

    uint64_t makeMask(unsigned bits) const;
    uint64_t signedDelta(stride_t delta) const;
    signature_t updateSignature(signature_t signature, stride_t delta) const;
    unsigned blockOffset(Addr addr) const;
    bool addDeltaToAddress(Addr base, stride_t delta, Addr &result) const;

    void updateRecentPages(Addr ppn);
    unsigned distinctRecentPages() const;
    unsigned globalAccuracy() const;
    void updatePCs(Addr pc);

    void readAndUpdateSignature(Addr ppn, bool secure, stride_t pageOffset,
                                signature_t &lastSignature,
                                signature_t &currentSignature, stride_t &delta,
                                bool &hit);
    void updatePattern(signature_t signature, stride_t delta);
    PatternEntry &getPattern(signature_t signature);
    void updateGhr(signature_t signature, unsigned confidence, stride_t offset,
                   stride_t delta);
    bool checkGhr(stride_t offset, GhrEntry &entry) const;

    void
    getPerceptronIndices(const FeatureRecord &record,
                         std::array<unsigned, NumFeatures> &indices) const;
    int predict(const FeatureRecord &record) const;
    void updateWeights(const FeatureRecord &record, bool useful);

    uint64_t filterHash(Addr addr, bool secure) const;
    unsigned filterIndex(uint64_t hash, unsigned remainderBits,
                         unsigned entries) const;
    uint64_t filterRemainder(uint64_t hash, unsigned remainderBits) const;
    bool filterMatch(const FilterEntry &entry, uint64_t remainder,
                     bool secure) const;
    bool inPrefetchQueue(Addr addr, bool secure) const;
    bool acceptedFilterContains(Addr addr, bool secure) const;
    bool pendingAcceptedContains(Addr addr, bool secure) const;
    void addPendingAccepted(Addr addr, bool secure,
                            const FeatureRecord &record);
    bool recordAccepted(Addr addr, bool secure, const FeatureRecord &record);
    void clearAccepted(Addr addr, bool secure);
    bool recordRejected(Addr addr, bool secure, const FeatureRecord &record);
    void trainDemand(Addr addr, bool secure);
    void trainEviction(Addr addr, bool secure);

  protected:
    bool
    supportsFillLevelHints() const override
    {
        return true;
    }

    void notifyEvict(const CacheDataUpdateProbeArg &info) override;
    void prefetchQueued(const PrefetchInfo &pfi,
                        const AddrPriority &addr_prio) override;
    void prefetchSquashed(const PrefetchInfo &pfi,
                          const AddrPriority &addr_prio) override;

  public:
    SPP_PPF(const SPP_PPFPrefetcherParams &p);
    ~SPP_PPF() = default;

    void notify(const CacheAccessProbeArg &acc,
                const PrefetchInfo &pfi) override;
    void calculatePrefetch(const PrefetchInfo &pfi,
                           std::vector<AddrPriority> &addresses,
                           const CacheAccessor &cache) override;
};

} // namespace prefetch
} // namespace gem5

#endif // __MEM_CACHE_PREFETCH_SPP_PPF_HH__
