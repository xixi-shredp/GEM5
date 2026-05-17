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
 * INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __MEM_CACHE_PREFETCH_FILL_LEVEL_HH__
#define __MEM_CACHE_PREFETCH_FILL_LEVEL_HH__

#include <cassert>
#include <cstdint>
#include <memory>

#include "base/extensible.hh"
#include "mem/request.hh"

namespace gem5
{
namespace prefetch
{

class FillLevelMetadata : public gem5::Extension<Request, FillLevelMetadata>
{
  public:
    explicit FillLevelMetadata(uint8_t skip_cache_levels)
        : skipCacheLevels(skip_cache_levels)
    {
        assert(skipCacheLevels < 4);
    }

    std::unique_ptr<ExtensionBase>
    clone() const override
    {
        return std::make_unique<FillLevelMetadata>(skipCacheLevels);
    }

    uint8_t skipCacheLevels;
};

inline uint8_t
prefetchSkipCacheLevels(const RequestPtr &req)
{
    auto metadata = req->getExtension<FillLevelMetadata>();
    return metadata ? metadata->skipCacheLevels : 0;
}

inline void
setPrefetchSkipCacheLevels(const RequestPtr &req, uint8_t levels)
{
    assert(levels < 4);
    if (levels == 0) {
        req->removeExtension<FillLevelMetadata>();
        return;
    }

    auto metadata = req->getExtension<FillLevelMetadata>();
    if (metadata) {
        metadata->skipCacheLevels = levels;
    } else {
        req->setExtension(std::make_shared<FillLevelMetadata>(levels));
    }
}

inline bool
consumePrefetchSkipCacheLevel(const RequestPtr &req)
{
    const uint8_t levels = prefetchSkipCacheLevels(req);
    if (levels == 0) {
        return false;
    }
    setPrefetchSkipCacheLevels(req, levels - 1);
    return true;
}

inline void
restorePrefetchSkipCacheLevel(const RequestPtr &req)
{
    const uint8_t levels = prefetchSkipCacheLevels(req);
    assert(levels < 3);
    setPrefetchSkipCacheLevels(req, levels + 1);
}

} // namespace prefetch
} // namespace gem5

#endif // __MEM_CACHE_PREFETCH_FILL_LEVEL_HH__
