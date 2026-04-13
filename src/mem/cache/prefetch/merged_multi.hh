/*
 * Copyright (c) 2026
 * All rights reserved.
 */

#ifndef __MEM_CACHE_PREFETCH_MERGED_MULTI_HH__
#define __MEM_CACHE_PREFETCH_MERGED_MULTI_HH__

#include <cstddef>

#include "mem/cache/prefetch/multi.hh"

namespace gem5
{

struct MergedMultiPrefetcherParams;

namespace prefetch
{

class MergedPacketQueue
{
  private:
    struct AddressKey
    {
        Addr addr;
        bool secure;

        bool
        operator==(const AddressKey &other) const
        {
            return addr == other.addr && secure == other.secure;
        }
    };

    struct AddressKeyHash
    {
        std::size_t
        operator()(const AddressKey &key) const
        {
            return std::hash<Addr>{}(key.addr) ^
                   (std::hash<bool>{}(key.secure) << 1);
        }
    };

    const std::size_t capacity;
    std::deque<PacketPtr> queue;
    std::unordered_set<AddressKey, AddressKeyHash> queuedAddresses;

    AddressKey
    keyFor(PacketPtr pkt) const
    {
        return AddressKey{pkt->getAddr(), pkt->isSecure()};
    }

  public:
    explicit MergedPacketQueue(std::size_t capacity) : capacity(capacity) {}

    ~MergedPacketQueue()
    {
        while (!queue.empty()) {
            delete queue.front();
            queue.pop_front();
        }
    }

    bool
    push(PacketPtr pkt)
    {
        const auto key = keyFor(pkt);
        if (queuedAddresses.find(key) != queuedAddresses.end()) {
            return false;
        }

        if (queue.size() >= capacity) {
            return false;
        }

        queue.push_back(pkt);
        queuedAddresses.insert(key);
        return true;
    }

    PacketPtr
    pop()
    {
        if (queue.empty()) {
            return nullptr;
        }

        PacketPtr pkt = queue.front();
        queue.pop_front();
        queuedAddresses.erase(keyFor(pkt));
        return pkt;
    }

    bool
    empty() const
    {
        return queue.empty();
    }

    bool
    full() const
    {
        return queue.size() >= capacity;
    }

    std::size_t
    size() const
    {
        return queue.size();
    }
};

class MergedMulti : public Multi
{
  private:
    MergedPacketQueue mergedQueue;

    void fillMergedQueue();

  public:
    explicit MergedMulti(const MergedMultiPrefetcherParams &p);

    PacketPtr getPacket() override;
    Tick nextPrefetchReadyTime() const override;
};

} // namespace prefetch
} // namespace gem5

#endif // __MEM_CACHE_PREFETCH_MERGED_MULTI_HH__
