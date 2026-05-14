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

#ifndef __MEM_INDIRECT_MEMORY_HINT_HH__
#define __MEM_INDIRECT_MEMORY_HINT_HH__

#include <algorithm>
#include <cctype>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "base/extensible.hh"
#include "base/types.hh"
#include "cpu/static_inst.hh"
#include "mem/request.hh"

namespace gem5
{

/**
 * Generic metadata used by indirect-memory prefetchers.
 *
 * Any ISA or CPU model can attach this extension to the demand source load's
 * Request after it decodes an address-indicating load.  The metadata describes
 * the loaded data layout rather than the instruction set, so contiguous vector
 * loads from Arm SVE, RVV, AVX, or scalar loads all use the same fields.
 */
class IndirectMemoryPrefetchHint
    : public Extension<Request, IndirectMemoryPrefetchHint>
{
  public:
    bool enabled = true;

    bool hasBase = false;
    Addr base = 0;
    int shift = 0;

    unsigned elementBytes = 0;
    unsigned elementStride = 0;
    unsigned elements = 0;
    std::vector<unsigned> elementOffsets;

    bool signedIndex = false;
    bool directPointer = false;
    bool trainTargets = true;

    IndirectMemoryPrefetchHint() = default;

    std::unique_ptr<ExtensionBase>
    clone() const override
    {
        return std::make_unique<IndirectMemoryPrefetchHint>(*this);
    }
};

struct ProcessorIndirectMemoryPrefetchTarget
{
    bool valid = false;
    Addr base = 0;
    int shift = 0;
};

inline ProcessorIndirectMemoryPrefetchTarget &
processorIndirectMemoryPrefetchTarget()
{
    static ProcessorIndirectMemoryPrefetchTarget target;
    return target;
}

inline void
setProcessorIndirectMemoryPrefetchTarget(bool enabled, Addr base, int shift)
{
    auto &target = processorIndirectMemoryPrefetchTarget();
    target.valid = enabled;
    target.base = base;
    target.shift = shift;
}

inline std::string
lowerIndirectHintMnemonic(const StaticInstPtr &inst)
{
    if (!inst) {
        return "";
    }

    std::string name = inst->getName();
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return name;
}

inline bool
indirectHintMnemonicHas(const std::string &name, const char *needle)
{
    return name.find(needle) != std::string::npos;
}

inline unsigned
activeIndirectHintBytes(unsigned access_size,
                        const std::vector<bool> &byte_enable)
{
    if (byte_enable.empty()) {
        return access_size;
    }

    return std::count(byte_enable.begin(), byte_enable.end(), true);
}

inline unsigned
inferIndirectMemoryHintElementBytes(const StaticInstPtr &inst,
                                    unsigned access_size)
{
    const std::string name = lowerIndirectHintMnemonic(inst);

    if (indirectHintMnemonicHas(name, "ld1sw") ||
        indirectHintMnemonicHas(name, "ld1w") ||
        indirectHintMnemonicHas(name, "vle32") ||
        indirectHintMnemonicHas(name, "vlse32") ||
        indirectHintMnemonicHas(name, "vluxei32") ||
        indirectHintMnemonicHas(name, "vloxei32") ||
        indirectHintMnemonicHas(name, "vmovd") ||
        indirectHintMnemonicHas(name, "movd") ||
        indirectHintMnemonicHas(name, "gatherd")) {
        return 4;
    }

    if (indirectHintMnemonicHas(name, "ld1d") ||
        indirectHintMnemonicHas(name, "vle64") ||
        indirectHintMnemonicHas(name, "vlse64") ||
        indirectHintMnemonicHas(name, "vluxei64") ||
        indirectHintMnemonicHas(name, "vloxei64") ||
        indirectHintMnemonicHas(name, "vmovq") ||
        indirectHintMnemonicHas(name, "movq") ||
        indirectHintMnemonicHas(name, "gatherq")) {
        return 8;
    }

    if (access_size > sizeof(uint64_t) && access_size % 4 == 0) {
        return 4;
    }

    return std::min<unsigned>(access_size, sizeof(uint64_t));
}

inline std::vector<unsigned>
inferIndirectMemoryHintElementOffsets(unsigned access_size,
                                      unsigned elem_bytes,
                                      const std::vector<bool> &byte_enable)
{
    std::vector<unsigned> offsets;
    if (elem_bytes == 0 || byte_enable.empty()) {
        return offsets;
    }

    bool masked = std::find(byte_enable.begin(), byte_enable.end(), false) !=
                  byte_enable.end();
    if (!masked) {
        return offsets;
    }

    unsigned bytes = std::min<unsigned>(access_size, byte_enable.size());
    for (unsigned offset = 0; offset + elem_bytes <= bytes;
         offset += elem_bytes) {
        bool lane_active = true;
        for (unsigned i = 0; i < elem_bytes; ++i) {
            lane_active &= byte_enable[offset + i];
        }
        if (lane_active) {
            offsets.push_back(offset);
        }
    }

    return offsets;
}

inline bool
inferIndirectMemoryHintSignedIndex(const StaticInstPtr &inst)
{
    const std::string name = lowerIndirectHintMnemonic(inst);
    return indirectHintMnemonicHas(name, "ld1sw") ||
           indirectHintMnemonicHas(name, "movsxd") ||
           indirectHintMnemonicHas(name, "sign");
}

inline bool
isIndirectMemoryHintSourceCandidate(const StaticInstPtr &inst, bool is_load,
                                    unsigned active_bytes)
{
    (void)active_bytes;
    if (!inst || !is_load || inst->isStore() || inst->isAtomic() ||
        inst->isPrefetch()) {
        return false;
    }

    const std::string name = lowerIndirectHintMnemonic(inst);
    return indirectHintMnemonicHas(name, "ld1sw") ||
           indirectHintMnemonicHas(name, "ld1sb") ||
           indirectHintMnemonicHas(name, "ld1sh") ||
           indirectHintMnemonicHas(name, "vlux") ||
           indirectHintMnemonicHas(name, "vlo") ||
           indirectHintMnemonicHas(name, "movsxd") ||
           indirectHintMnemonicHas(name, "gather");
}

inline std::shared_ptr<IndirectMemoryPrefetchHint>
getOrCreateIndirectMemoryPrefetchHint(Request *req)
{
    if (req == nullptr) {
        return nullptr;
    }

    auto hint = req->getExtension<IndirectMemoryPrefetchHint>();
    if (hint == nullptr) {
        hint = std::make_shared<IndirectMemoryPrefetchHint>();
        req->setExtension(hint);
    }
    return hint;
}

inline void
updateIndirectMemoryPrefetchTarget(
    const std::shared_ptr<IndirectMemoryPrefetchHint> &hint, Addr base,
    int shift)
{
    if (hint == nullptr) {
        return;
    }

    hint->enabled = true;
    hint->hasBase = true;
    hint->base = base;
    hint->shift = shift;
}

inline void
setIndirectMemoryPrefetchTarget(Request *req, Addr base, int shift)
{
    auto hint = getOrCreateIndirectMemoryPrefetchHint(req);
    if (hint == nullptr) {
        return;
    }

    updateIndirectMemoryPrefetchTarget(hint, base, shift);
}

inline void
setIndirectMemoryPrefetchTarget(const RequestPtr &req, Addr base, int shift)
{
    setIndirectMemoryPrefetchTarget(req.get(), base, shift);
}

inline bool
applyProcessorIndirectMemoryPrefetchTarget(Request *req)
{
    const auto &target = processorIndirectMemoryPrefetchTarget();
    if (!target.valid) {
        return false;
    }

    setIndirectMemoryPrefetchTarget(req, target.base, target.shift);
    return true;
}

inline int
indirectMemoryPrefetchScaleToShift(uint64_t scale)
{
    switch (scale) {
        case 1:
            return 0;
        case 2:
            return 1;
        case 4:
            return 2;
        case 8:
            return 3;
        default:
            return 0;
    }
}

struct IndirectMemoryPrefetchRegKey
{
    ContextID context = InvalidContextID;
    RegClassType regClass = InvalidRegClass;
    RegIndex regIndex = 0;

    bool
    operator<(const IndirectMemoryPrefetchRegKey &other) const
    {
        return std::tie(context, regClass, regIndex) <
               std::tie(other.context, other.regClass, other.regIndex);
    }
};

struct IndirectMemoryPrefetchProducerKey
{
    ContextID context = InvalidContextID;
    RegClassType regClass = InvalidRegClass;
    RegIndex regIndex = 0;
    Addr pc = 0;

    bool
    operator<(const IndirectMemoryPrefetchProducerKey &other) const
    {
        return std::tie(context, regClass, regIndex, pc) <
               std::tie(other.context, other.regClass, other.regIndex,
                        other.pc);
    }
};

struct IndirectMemoryPrefetchDataflowTarget
{
    bool valid = false;
    Addr base = 0;
    int shift = 0;
};

struct IndirectMemoryPrefetchProducerRecord
{
    IndirectMemoryPrefetchProducerKey key;
    std::weak_ptr<IndirectMemoryPrefetchHint> hint;
};

inline bool
indirectMemoryPrefetchTrackableReg(const RegId &reg)
{
    switch (reg.classValue()) {
        case IntRegClass:
        case FloatRegClass:
        case VecRegClass:
        case VecElemClass:
        case MatRegClass:
            return true;
        default:
            return false;
    }
}

inline IndirectMemoryPrefetchRegKey
indirectMemoryPrefetchRegKey(ContextID context, const RegId &reg)
{
    return {context, reg.classValue(), reg.index()};
}

inline IndirectMemoryPrefetchProducerKey
indirectMemoryPrefetchProducerKey(ContextID context, Addr pc, const RegId &reg)
{
    return {context, reg.classValue(), reg.index(), pc};
}

class IndirectMemoryPrefetchDataflowTracker
{
  private:
    static constexpr size_t MaxLearnedTargets = 256;

    std::map<IndirectMemoryPrefetchRegKey,
             IndirectMemoryPrefetchProducerRecord>
        latestProducers;
    std::map<IndirectMemoryPrefetchProducerKey,
             IndirectMemoryPrefetchDataflowTarget>
        learnedTargets;
    std::deque<IndirectMemoryPrefetchProducerKey> learnedOrder;

    void
    rememberTarget(const IndirectMemoryPrefetchProducerKey &key,
                   const IndirectMemoryPrefetchDataflowTarget &target)
    {
        const bool is_new = learnedTargets.find(key) == learnedTargets.end();
        learnedTargets[key] = target;
        if (is_new) {
            learnedOrder.push_back(key);
        }

        while (learnedOrder.size() > MaxLearnedTargets) {
            learnedTargets.erase(learnedOrder.front());
            learnedOrder.pop_front();
        }
    }

  public:
    void
    registerProducer(ContextID context, Addr pc, const RegId &reg,
                     const std::shared_ptr<IndirectMemoryPrefetchHint> &hint)
    {
        if (!indirectMemoryPrefetchTrackableReg(reg) || hint == nullptr) {
            return;
        }

        const auto reg_key = indirectMemoryPrefetchRegKey(context, reg);
        const auto producer_key =
            indirectMemoryPrefetchProducerKey(context, pc, reg);
        latestProducers[reg_key] = {producer_key, hint};

        auto learned = learnedTargets.find(producer_key);
        if (learned != learnedTargets.end() && learned->second.valid) {
            updateIndirectMemoryPrefetchTarget(hint, learned->second.base,
                                               learned->second.shift);
        }
    }

    bool
    applyTarget(ContextID context, Addr pc, const RegId &reg,
                const std::shared_ptr<IndirectMemoryPrefetchHint> &hint) const
    {
        if (!indirectMemoryPrefetchTrackableReg(reg) || hint == nullptr) {
            return false;
        }

        const auto producer_key =
            indirectMemoryPrefetchProducerKey(context, pc, reg);
        auto learned = learnedTargets.find(producer_key);
        if (learned == learnedTargets.end() || !learned->second.valid) {
            return false;
        }

        updateIndirectMemoryPrefetchTarget(hint, learned->second.base,
                                           learned->second.shift);
        return true;
    }

    bool
    bindTarget(ContextID context, const RegId &source_reg, Addr base,
               int shift)
    {
        if (!indirectMemoryPrefetchTrackableReg(source_reg)) {
            return false;
        }

        const auto reg_key = indirectMemoryPrefetchRegKey(context, source_reg);
        auto producer = latestProducers.find(reg_key);
        if (producer == latestProducers.end()) {
            return false;
        }

        IndirectMemoryPrefetchDataflowTarget target;
        target.valid = true;
        target.base = base;
        target.shift = shift;
        rememberTarget(producer->second.key, target);

        if (auto hint = producer->second.hint.lock()) {
            updateIndirectMemoryPrefetchTarget(hint, base, shift);
        }
        return true;
    }
};

inline IndirectMemoryPrefetchDataflowTracker &
indirectMemoryPrefetchDataflowTracker()
{
    static IndirectMemoryPrefetchDataflowTracker tracker;
    return tracker;
}

inline void
registerIndirectMemoryPrefetchSourceProducer(
    Request *req, const StaticInstPtr &inst,
    const std::shared_ptr<IndirectMemoryPrefetchHint> &hint)
{
    if (req == nullptr || !inst || hint == nullptr || !req->hasContextId() ||
        !req->hasPC()) {
        return;
    }

    auto &tracker = indirectMemoryPrefetchDataflowTracker();
    for (int i = 0; i < inst->numDestRegs(); ++i) {
        tracker.registerProducer(req->contextId(), req->getPC(),
                                 inst->destRegIdx(i), hint);
    }
}

inline bool
applyIndirectMemoryPrefetchDataflowTarget(
    Request *req, const StaticInstPtr &inst,
    const std::shared_ptr<IndirectMemoryPrefetchHint> &hint)
{
    if (req == nullptr || !inst || hint == nullptr || !req->hasContextId() ||
        !req->hasPC()) {
        return false;
    }

    bool applied = false;
    auto &tracker = indirectMemoryPrefetchDataflowTracker();
    for (int i = 0; i < inst->numDestRegs(); ++i) {
        applied |= tracker.applyTarget(req->contextId(), req->getPC(),
                                       inst->destRegIdx(i), hint);
    }
    return applied;
}

inline bool
bindIndirectMemoryPrefetchTarget(ContextID context, const RegId &source_reg,
                                 Addr base, int shift)
{
    return indirectMemoryPrefetchDataflowTracker().bindTarget(
        context, source_reg, base, shift);
}

inline void
annotateIndirectMemoryPrefetchHint(Request *req, const StaticInstPtr &inst,
                                   bool is_load, unsigned access_size,
                                   const std::vector<bool> &byte_enable)
{
    if (!req) {
        return;
    }

    auto hint = req->getExtension<IndirectMemoryPrefetchHint>();
    bool had_hint = hint != nullptr;
    unsigned active_bytes = activeIndirectHintBytes(access_size, byte_enable);
    bool candidate =
        isIndirectMemoryHintSourceCandidate(inst, is_load, active_bytes);
    if ((!candidate && hint == nullptr) || active_bytes == 0) {
        req->removeExtension<IndirectMemoryPrefetchHint>();
        return;
    }

    if (hint == nullptr) {
        hint = std::make_shared<IndirectMemoryPrefetchHint>();
    }
    if (!hint->enabled) {
        req->setExtension(hint);
        return;
    }

    unsigned elem_bytes =
        hint->elementBytes != 0
            ? hint->elementBytes
            : inferIndirectMemoryHintElementBytes(inst, active_bytes);
    if (elem_bytes == 0) {
        req->removeExtension<IndirectMemoryPrefetchHint>();
        return;
    }

    std::vector<unsigned> offsets = inferIndirectMemoryHintElementOffsets(
        access_size, elem_bytes, byte_enable);
    bool masked = !byte_enable.empty() &&
                  std::find(byte_enable.begin(), byte_enable.end(), false) !=
                      byte_enable.end();
    if (masked && offsets.empty()) {
        req->removeExtension<IndirectMemoryPrefetchHint>();
        return;
    }

    hint->enabled = true;
    hint->elementBytes = elem_bytes;
    if (hint->elementStride == 0) {
        hint->elementStride = elem_bytes;
    }
    if (!offsets.empty() || masked) {
        hint->elementOffsets = inferIndirectMemoryHintElementOffsets(
            access_size, elem_bytes, byte_enable);
        hint->elements = hint->elementOffsets.size();
    } else if (hint->elements == 0) {
        hint->elements = std::max<unsigned>(1, active_bytes / elem_bytes);
    }
    hint->signedIndex =
        hint->signedIndex || inferIndirectMemoryHintSignedIndex(inst);
    if (!had_hint) {
        hint->trainTargets = true;
    }
    if (!hint->hasBase) {
        applyIndirectMemoryPrefetchDataflowTarget(req, inst, hint);
    }
    req->setExtension(hint);
    if (candidate && !hint->hasBase) {
        applyProcessorIndirectMemoryPrefetchTarget(req);
        hint = req->getExtension<IndirectMemoryPrefetchHint>();
    }
    if (candidate || had_hint) {
        registerIndirectMemoryPrefetchSourceProducer(req, inst, hint);
    }
}

inline void
annotateIndirectMemoryPrefetchHint(const RequestPtr &req,
                                   const StaticInstPtr &inst, bool is_load,
                                   unsigned access_size,
                                   const std::vector<bool> &byte_enable)
{
    annotateIndirectMemoryPrefetchHint(req.get(), inst, is_load, access_size,
                                       byte_enable);
}

} // namespace gem5

#endif // __MEM_INDIRECT_MEMORY_HINT_HH__
