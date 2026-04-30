/*
 * Copyright (c) 2026
 * All rights reserved.
 */

#ifndef __MEM_IDEAL_CACHE_HH__
#define __MEM_IDEAL_CACHE_HH__

#include <vector>

#include "mem/port.hh"
#include "params/IdealCache.hh"
#include "sim/clocked_object.hh"
#include "sim/eventq.hh"

namespace gem5
{

class System;

bool idealCacheNeedsAtomicAccess(const PacketPtr pkt);

class IdealCache : public ClockedObject
{
  private:
    class CPUSidePort : public ResponsePort
    {
      private:
        IdealCache *owner;
        const PortID id;
        bool needRetry = false;
        PacketPtr blockedPacket = nullptr;

      public:
        CPUSidePort(const std::string &name, PortID id, IdealCache *owner);

        bool sendPacket(PacketPtr pkt);
        void trySendRetry();

      protected:
        Tick recvAtomic(PacketPtr pkt) override;
        void recvFunctional(PacketPtr pkt) override;
        bool recvTimingReq(PacketPtr pkt) override;
        void recvRespRetry() override;
        AddrRangeList getAddrRanges() const override;
    };

    class MemSidePort : public RequestPort
    {
      private:
        IdealCache *owner;

      public:
        MemSidePort(const std::string &name, IdealCache *owner);

      protected:
        bool recvTimingResp(PacketPtr pkt) override;
        void recvReqRetry() override;
        void recvRangeChange() override;
    };

    struct PendingAccess
    {
        PacketPtr pkt = nullptr;
        PortID port = InvalidPortID;
        bool needsResponse = false;
    };

    std::vector<CPUSidePort> cpuPorts;
    MemSidePort memPort;
    System *const system;

    const Tick hitLatency;

    bool blocked = false;
    PendingAccess pendingAccess;
    EventFunctionWrapper releaseEvent;

    bool handleTimingReq(PacketPtr pkt, PortID port_id);
    Tick handleAtomicReq(PacketPtr pkt);
    void handleFunctionalReq(PacketPtr pkt);
    void performIdealAccess(PacketPtr pkt);
    void completeAccess();
    void releaseBlocked();
    AddrRangeList getAddrRanges() const;

  public:
    IdealCache(const IdealCacheParams &params);

    void init() override;
    Port &getPort(const std::string &if_name,
                  PortID idx = InvalidPortID) override;
};

} // namespace gem5

#endif // __MEM_IDEAL_CACHE_HH__
