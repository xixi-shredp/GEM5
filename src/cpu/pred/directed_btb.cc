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

#include "cpu/pred/directed_btb.hh"

#include <vector>

#include "base/intmath.hh"
#include "base/logging.hh"

namespace gem5::branch_prediction
{

DirectedBTBEntry::DirectedBTBEntry(TagExtractor ext, unsigned counter_bits,
                                   unsigned initial_counter)
    : BTBEntry(ext),
      counterBits(counter_bits),
      takenCounter(counter_bits, initial_counter),
      conditional(false)
{}

DirectedBTBEntry::DirectedBTBEntry(const DirectedBTBEntry &other)
    : BTBEntry(other),
      counterBits(other.counterBits),
      takenCounter(other.takenCounter),
      conditional(other.conditional)
{}

DirectedBTBEntry &
DirectedBTBEntry::operator=(const DirectedBTBEntry &other)
{
    BTBEntry::operator=(other);
    counterBits = other.counterBits;
    takenCounter = other.takenCounter;
    conditional = other.conditional;
    return *this;
}

void
DirectedBTBEntry::resetDirectionState(bool is_conditional,
                                      unsigned initial_counter)
{
    conditional = is_conditional;
    takenCounter = SatCounter8(counterBits, initial_counter);
}

void
DirectedBTBEntry::updateDirection(bool taken)
{
    if (!conditional) {
        return;
    }

    if (taken) {
        ++takenCounter;
    } else {
        --takenCounter;
    }
}

bool
DirectedBTBEntry::directsTaken(unsigned threshold) const
{
    return isValid() && conditional && (takenCounter >= threshold);
}

bool
DirectedBTBEntry::replacementCandidate(unsigned threshold) const
{
    return !isValid() || (conditional && (takenCounter < threshold));
}

DirectedBTB::DirectedBTB(const Params &p)
    : BranchTargetBuffer(p),
      btb("directedBTB", p.numEntries, p.associativity, p.btbReplPolicy,
          p.btbIndexingPolicy,
          DirectedBTBEntry(genTagExtractor(p.btbIndexingPolicy),
                           p.takenCounterBits, p.initialTakenCounter)),
      replPolicy(p.btbReplPolicy),
      initialTakenCounter(p.initialTakenCounter),
      directedTakenThreshold(p.directedTakenThreshold),
      replacementCandidateThreshold(p.replacementCandidateThreshold)
{
    fatal_if(!isPowerOf2(p.numEntries / p.associativity),
             "BTB sets is not a power of 2!");
    fatal_if(p.takenCounterBits == 0 || p.takenCounterBits > 8,
             "DirectedBTB takenCounterBits must be in the range [1, 8]");

    const unsigned max_counter = (1u << p.takenCounterBits) - 1;
    fatal_if(p.initialTakenCounter > max_counter,
             "DirectedBTB initialTakenCounter exceeds counter width");
    fatal_if(p.directedTakenThreshold > max_counter,
             "DirectedBTB directedTakenThreshold exceeds counter width");
    fatal_if(
        p.replacementCandidateThreshold > max_counter,
        "DirectedBTB replacementCandidateThreshold exceeds counter width");
}

void
DirectedBTB::memInvalidate()
{
    btb.clear();
}

bool
DirectedBTB::isConditionalBranch(BranchType type)
{
    return type == BranchType::DirectCond || type == BranchType::IndirectCond;
}

DirectedBTBEntry *
DirectedBTB::findEntry(Addr instPC, ThreadID tid)
{
    return btb.findEntry({instPC, tid});
}

DirectedBTBEntry *
DirectedBTB::findVictim(Addr instPC, ThreadID tid)
{
    const BTBTagType::KeyType key{instPC, tid};
    auto all_candidates = btb.getPossibleEntries(key);

    std::vector<ReplaceableEntry *> weak_candidates;
    weak_candidates.reserve(all_candidates.size());

    std::vector<ReplaceableEntry *> all_replaceable;
    all_replaceable.reserve(all_candidates.size());

    for (auto *candidate : all_candidates) {
        all_replaceable.push_back(candidate);
        if (candidate->replacementCandidate(replacementCandidateThreshold)) {
            weak_candidates.push_back(candidate);
        }
    }

    auto &candidate_pool =
        weak_candidates.empty() ? all_replaceable : weak_candidates;
    auto *victim =
        static_cast<DirectedBTBEntry *>(replPolicy->getVictim(candidate_pool));

    if (victim->isValid()) {
        stats.evictions++;
    }

    btb.invalidate(victim);
    return victim;
}

bool
DirectedBTB::valid(ThreadID tid, Addr instPC)
{
    return findEntry(instPC, tid) != nullptr;
}

const PCStateBase *
DirectedBTB::lookup(ThreadID tid, Addr instPC, BranchType type)
{
    stats.lookups[type]++;

    DirectedBTBEntry *entry = btb.accessEntry({instPC, tid});
    if (entry) {
        return entry->target.get();
    }

    stats.misses[type]++;
    return nullptr;
}

const StaticInstPtr
DirectedBTB::getInst(ThreadID tid, Addr instPC)
{
    DirectedBTBEntry *entry = findEntry(instPC, tid);
    if (entry) {
        return entry->inst;
    }

    return nullptr;
}

bool
DirectedBTB::lookupDirectionHint(ThreadID tid, Addr instPC, BranchType type)
{
    if (!isConditionalBranch(type)) {
        return false;
    }

    DirectedBTBEntry *entry = findEntry(instPC, tid);
    return entry != nullptr && entry->directsTaken(directedTakenThreshold);
}

void
DirectedBTB::update(ThreadID tid, Addr instPC, const PCStateBase &target,
                    BranchType type, StaticInstPtr inst)
{
    stats.updates[type]++;

    DirectedBTBEntry *entry = findEntry(instPC, tid);
    if (entry == nullptr) {
        entry = findVictim(instPC, tid);
        btb.insertEntry({instPC, tid}, entry);
        entry->resetDirectionState(isConditionalBranch(type),
                                   initialTakenCounter);
    }

    btb.accessEntry(entry);
    entry->update(target, inst);
}

void
DirectedBTB::updateDirectionInfo(ThreadID tid, Addr instPC, BranchType type,
                                 bool actually_taken)
{
    if (!isConditionalBranch(type)) {
        return;
    }

    DirectedBTBEntry *entry = findEntry(instPC, tid);
    if (entry == nullptr) {
        return;
    }

    entry->updateDirection(actually_taken);
}

} // namespace gem5::branch_prediction
