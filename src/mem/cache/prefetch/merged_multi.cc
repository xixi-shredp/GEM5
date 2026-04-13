/*
 * Copyright (c) 2026
 * All rights reserved.
 */

#include "mem/cache/prefetch/merged_multi.hh"

#include "base/logging.hh"
#include "params/MergedMultiPrefetcher.hh"

namespace gem5
{

namespace prefetch
{

MergedMulti::MergedMulti(const MergedMultiPrefetcherParams &p)
    : Multi(p), mergedQueue(p.queue_size)
{
    fatal_if(p.queue_size == 0,
             "MergedMultiPrefetcher queue_size must be greater than zero.");
}

void
MergedMulti::fillMergedQueue()
{
    if (prefetchers.empty() || mergedQueue.full()) {
        return;
    }

    bool madeProgress = false;
    do {
        madeProgress = false;
        lastChosenPf = (lastChosenPf + 1) % prefetchers.size();
        uint8_t pfTurn = lastChosenPf;

        for (int pf = 0; pf < prefetchers.size(); ++pf) {
            if (mergedQueue.full()) {
                return;
            }

            if (prefetchers[pfTurn]->nextPrefetchReadyTime() <= curTick()) {
                PacketPtr pkt = prefetchers[pfTurn]->getPacket();
                panic_if(!pkt,
                         "Prefetcher is ready but didn't return a packet.");
                if (!mergedQueue.push(pkt)) {
                    delete pkt;
                }
                madeProgress = true;
            }

            pfTurn = (pfTurn + 1) % prefetchers.size();
        }
    } while (madeProgress && !mergedQueue.full());
}

PacketPtr
MergedMulti::getPacket()
{
    if (mergedQueue.empty()) {
        fillMergedQueue();
    }

    PacketPtr pkt = mergedQueue.pop();
    if (pkt) {
        prefetchStats.pfIssued++;
        issuedPrefetches++;
    }

    return pkt;
}

Tick
MergedMulti::nextPrefetchReadyTime() const
{
    if (!mergedQueue.empty()) {
        return curTick();
    }

    return Multi::nextPrefetchReadyTime();
}

} // namespace prefetch
} // namespace gem5
