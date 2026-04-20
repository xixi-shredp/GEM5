#ifndef __MEM_CACHE_TAGS_PARTITIONING_POLICIES_KAIROS_MANAGER_HH__
#define __MEM_CACHE_TAGS_PARTITIONING_POLICIES_KAIROS_MANAGER_HH__

#include <string>
#include <unordered_set>
#include <vector>

#include "mem/cache/tags/partitioning_policies/partition_manager.hh"
#include "params/KairosPartitionManager.hh"

namespace gem5
{
namespace partitioning_policy
{

/**
 * A PartitionManager that assigns partition_id=1 to packets whose
 * RequestorID matches any of the names listed in `metadata_requestor_names`
 * (resolved from System at init()). Everything else maps to partition_id=0.
 *
 * This is used with WayPartitioningPolicy to model the Kairos paper's
 * setup where a subset of LLC ways is reserved for temporal-prefetcher
 * metadata traffic.
 */
class KairosPartitionManager : public PartitionManager
{
  public:
    PARAMS(KairosPartitionManager);
    KairosPartitionManager(const Params &p);

    uint64_t readPacketPartitionID(PacketPtr pkt) const override;

    void init() override;

  private:
    const std::vector<std::string> metadataRequestorNames;
    std::unordered_set<RequestorID> metadataRequestorIds;
    bool resolved = false;
};

} // namespace partitioning_policy
} // namespace gem5

#endif // __MEM_CACHE_TAGS_PARTITIONING_POLICIES_KAIROS_MANAGER_HH__
