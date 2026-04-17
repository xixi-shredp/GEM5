#include "mem/cache/tags/partitioning_policies/kairos_partition_manager.hh"

#include "base/logging.hh"
#include "base/trace.hh"
#include "debug/PartitionPolicy.hh"
#include "mem/packet.hh"
#include "mem/request.hh"
#include "sim/system.hh"

namespace gem5
{
namespace partitioning_policy
{

KairosPartitionManager::KairosPartitionManager(const Params &p)
    : PartitionManager(p), metadataRequestorNames(p.metadata_requestor_names)
{}

void
KairosPartitionManager::init()
{
    // Note: this runs after all SimObjects have been registered, so
    // RequestorIDs issued during construction are visible here.
    PartitionManager::init();
    if (resolved) {
        return;
    }

    // params().system is the System SimObject; we need it to translate
    // names to RequestorIDs. Grab it via the params struct.
    System *sys = params().system;
    warn_if(sys == nullptr,
            "KairosPartitionManager has no System pointer; metadata "
            "requestor names will not be resolved.");
    if (!sys) {
        return;
    }

    for (RequestorID rid = 0; rid < sys->maxRequestors(); rid++) {
        const std::string &name = sys->getRequestorName(rid);
        for (const auto &pattern : metadataRequestorNames) {
            if (name.find(pattern) != std::string::npos) {
                metadataRequestorIds.insert(rid);
                inform("KairosPartitionManager: RequestorID %u (%s) tagged "
                       "as metadata (pattern '%s')",
                       rid, name.c_str(), pattern.c_str());
                break;
            }
        }
    }
    resolved = true;
}

uint64_t
KairosPartitionManager::readPacketPartitionID(PacketPtr pkt) const
{
    if (!pkt || !pkt->req) {
        return 0;
    }
    const RequestorID rid = pkt->req->requestorId();
    return metadataRequestorIds.count(rid) ? 1 : 0;
}

} // namespace partitioning_policy
} // namespace gem5
