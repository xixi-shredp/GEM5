/*
 * Copyright (c) 2026
 * All rights reserved.
 */

#include "mem/cache/ideal_cache.hh"

#include "base/cprintf.hh"
#include "base/logging.hh"
#include "base/trace.hh"

namespace gem5
{

bool
idealCacheNeedsAtomicAccess(const PacketPtr pkt)
{
    return pkt->isLLSC() || pkt->req->isSwap() || pkt->req->isAtomic();
}

IdealCache::CPUSidePort::CPUSidePort(const std::string &name, PortID id,
                                     IdealCache *owner)
    : ResponsePort(name), owner(owner), id(id)
{}

Tick
IdealCache::CPUSidePort::recvAtomic(PacketPtr pkt)
{
    return owner->handleAtomicReq(pkt);
}

void
IdealCache::CPUSidePort::recvFunctional(PacketPtr pkt)
{
    owner->handleFunctionalReq(pkt);
}

bool
IdealCache::CPUSidePort::recvTimingReq(PacketPtr pkt)
{
    return owner->handleTimingReq(pkt, id);
}

void
IdealCache::CPUSidePort::recvRespRetry()
{
    assert(blockedPacket != nullptr);
    PacketPtr pkt = blockedPacket;
    blockedPacket = nullptr;
    if (sendPacket(pkt)) {
        owner->processReadyResponses();
    }
}

AddrRangeList
IdealCache::CPUSidePort::getAddrRanges() const
{
    return owner->getAddrRanges();
}

bool
IdealCache::CPUSidePort::sendPacket(PacketPtr pkt)
{
    panic_if(blockedPacket != nullptr, "Should never try to send if blocked");

    if (!sendTimingResp(pkt)) {
        blockedPacket = pkt;
        return false;
    }

    return true;
}

IdealCache::MemSidePort::MemSidePort(const std::string &name,
                                     IdealCache *owner)
    : RequestPort(name), owner(owner)
{}

bool
IdealCache::MemSidePort::recvTimingResp(PacketPtr pkt)
{
    panic("IdealCache should not receive timing responses: %s", pkt->print());
}

void
IdealCache::MemSidePort::recvReqRetry()
{
    panic("IdealCache should not receive timing request retries");
}

void
IdealCache::MemSidePort::recvRangeChange()
{
    for (auto &port : owner->cpuPorts) {
        port.sendRangeChange();
    }
}

IdealCache::IdealCache(const IdealCacheParams &params)
    : ClockedObject(params),
      memPort(params.name + ".mem_side", this),
      hitLatency(params.hit_latency),
      sendEvent([this] { processReadyResponses(); }, name())
{
    for (int i = 0; i < params.port_cpu_side_connection_count; ++i) {
        cpuPorts.emplace_back(name() + csprintf(".cpu_side[%d]", i), i, this);
    }
}

void
IdealCache::init()
{
    if (cpuPorts.empty() || !memPort.isConnected()) {
        fatal("IdealCache %s must connect cpu_side and mem_side ports\n",
              name());
    }

    for (auto &port : cpuPorts) {
        if (!port.isConnected()) {
            fatal("IdealCache %s has an unconnected cpu_side port\n", name());
        }
    }
}

Port &
IdealCache::getPort(const std::string &if_name, PortID idx)
{
    if (if_name == "mem_side") {
        panic_if(idx != InvalidPortID, "mem_side is not a vector port on %s",
                 name());
        return memPort;
    } else if (if_name == "cpu_side" && idx < cpuPorts.size()) {
        return cpuPorts[idx];
    }

    return ClockedObject::getPort(if_name, idx);
}

bool
IdealCache::handleTimingReq(PacketPtr pkt, PortID port_id)
{
    const bool needs_response = pkt->needsResponse();
    performIdealAccess(pkt);
    if (!needs_response) {
        pendingDelete.reset(pkt);
        return true;
    }

    const Tick ready_tick = curTick() + hitLatency;
    auto it = pendingResponses.end();
    while (it != pendingResponses.begin()) {
        auto prev = std::prev(it);
        if (prev->readyTick <= ready_tick) {
            break;
        }
        it = prev;
    }
    pendingResponses.insert(it, PendingResponse{pkt, port_id, ready_tick});
    scheduleNextReadyResponse();
    return true;
}

Tick
IdealCache::handleAtomicReq(PacketPtr pkt)
{
    performIdealAccess(pkt);
    return hitLatency;
}

void
IdealCache::handleFunctionalReq(PacketPtr pkt)
{
    memPort.sendFunctional(pkt);
}

void
IdealCache::performIdealAccess(PacketPtr pkt)
{
    if (idealCacheNeedsAtomicAccess(pkt)) {
        memPort.sendAtomic(pkt);
    } else {
        memPort.sendFunctional(pkt);
    }
}

void
IdealCache::processReadyResponses()
{
    for (auto it = pendingResponses.begin(); it != pendingResponses.end();) {
        if (it->readyTick > curTick()) {
            break;
        }

        const PortID port_id = it->port;
        panic_if(port_id == InvalidPortID,
                 "IdealCache %s lost the source port for %s", name(),
                 it->pkt->print());

        auto &port = cpuPorts[port_id];
        if (port.blocked()) {
            ++it;
            continue;
        }

        PacketPtr pkt = it->pkt;
        if (port.sendPacket(pkt)) {
            it = pendingResponses.erase(it);
        } else {
            it = pendingResponses.erase(it);
        }
    }

    scheduleNextReadyResponse();
}

void
IdealCache::scheduleNextReadyResponse()
{
    if (sendEvent.scheduled()) {
        deschedule(sendEvent);
    }

    Tick next_tick = MaxTick;
    for (const auto &response : pendingResponses) {
        const auto &port = cpuPorts[response.port];
        if (port.blocked()) {
            continue;
        }
        next_tick = response.readyTick;
        break;
    }

    if (next_tick != MaxTick) {
        schedule(sendEvent, std::max(next_tick, curTick()));
    }
}

AddrRangeList
IdealCache::getAddrRanges() const
{
    return memPort.getAddrRanges();
}

} // namespace gem5
