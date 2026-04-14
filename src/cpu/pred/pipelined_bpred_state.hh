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

#ifndef __CPU_PRED_PIPELINED_BPRED_STATE_HH__
#define __CPU_PRED_PIPELINED_BPRED_STATE_HH__

#include <algorithm>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <vector>

#include "base/types.hh"
#include "cpu/inst_seq.hh"

namespace gem5
{

namespace branch_prediction
{

struct StagePredictionState
{
    void *bpHistory = nullptr;
    bool completed = false;
    bool hasPrediction = false;
    bool predTaken = false;
    std::optional<bool> speculativeTaken;
    std::optional<Addr> speculativeTarget;
};

class PipelineStageHistoryStorage
{
  public:
    PipelineStageHistoryStorage() = default;
    explicit PipelineStageHistoryStorage(size_t numStages)
        : stageHistories(numStages, nullptr)
    {}

    void
    resize(size_t numStages)
    {
        stageHistories.assign(numStages, nullptr);
    }

    size_t
    size() const
    {
        return stageHistories.size();
    }

    bool
    empty() const
    {
        return std::none_of(
            stageHistories.begin(), stageHistories.end(),
            [](const void *history) { return history != nullptr; });
    }

    void
    record(size_t stageIndex, void *history)
    {
        if (stageIndex >= stageHistories.size()) {
            throw std::out_of_range("stage index out of range");
        }
        stageHistories[stageIndex] = history;
    }

    bool
    hasHistory(size_t stageIndex) const
    {
        if (stageIndex >= stageHistories.size()) {
            throw std::out_of_range("stage index out of range");
        }
        return stageHistories[stageIndex] != nullptr;
    }

    void *
    get(size_t stageIndex) const
    {
        if (stageIndex >= stageHistories.size()) {
            throw std::out_of_range("stage index out of range");
        }
        return stageHistories[stageIndex];
    }

    void *
    take(size_t stageIndex)
    {
        if (stageIndex >= stageHistories.size()) {
            throw std::out_of_range("stage index out of range");
        }
        void *history = stageHistories[stageIndex];
        stageHistories[stageIndex] = nullptr;
        return history;
    }

  private:
    std::vector<void *> stageHistories;
};

struct StageSpeculation
{
    bool taken;
    std::optional<Addr> target;
    enum class TargetSource
    {
        Unresolved,
        Fallthrough,
        FrontendTarget,
        BtbTarget,
    } targetSource = TargetSource::Unresolved;
};

using StageTargetSource = StageSpeculation::TargetSource;

struct StageTargetResolution
{
    std::optional<Addr> target;
    StageTargetSource source = StageTargetSource::Unresolved;
};

struct StageHistoryUpdateInput
{
    bool taken;
    Addr target;
    bool usedFallbackTarget;
};

inline StageHistoryUpdateInput
resolveStageHistoryUpdateInput(
    const std::optional<StageSpeculation> &speculation, bool fallbackTaken,
    Addr fallbackTarget)
{
    if (!speculation.has_value()) {
        return StageHistoryUpdateInput{fallbackTaken, fallbackTarget, true};
    }

    return StageHistoryUpdateInput{
        speculation->taken, speculation->target.value_or(fallbackTarget),
        !speculation->target.has_value()};
}

inline StageTargetResolution
resolveStageSpeculativeTarget(bool stageTaken, bool frontendTaken,
                              std::optional<Addr> frontendTarget,
                              std::optional<Addr> btbTarget, Addr fallthrough)
{
    if (!stageTaken) {
        return StageTargetResolution{fallthrough,
                                     StageTargetSource::Fallthrough};
    }

    if (frontendTaken && frontendTarget.has_value()) {
        return StageTargetResolution{frontendTarget,
                                     StageTargetSource::FrontendTarget};
    }

    if (btbTarget.has_value()) {
        return StageTargetResolution{btbTarget, StageTargetSource::BtbTarget};
    }

    return StageTargetResolution{std::nullopt, StageTargetSource::Unresolved};
}

class PipelineStageSpeculationStorage
{
  public:
    PipelineStageSpeculationStorage() = default;
    explicit PipelineStageSpeculationStorage(size_t numStages)
        : stageSpeculations(numStages)
    {}

    void
    resize(size_t numStages)
    {
        stageSpeculations.assign(numStages, std::nullopt);
    }

    void
    record(size_t stageIndex, bool taken, std::optional<Addr> target,
           StageTargetSource source = StageTargetSource::Unresolved)
    {
        if (stageIndex >= stageSpeculations.size()) {
            throw std::out_of_range("stage index out of range");
        }

        stageSpeculations[stageIndex] =
            StageSpeculation{taken, target, source};
    }

    bool
    hasSpeculation(size_t stageIndex) const
    {
        if (stageIndex >= stageSpeculations.size()) {
            throw std::out_of_range("stage index out of range");
        }
        return stageSpeculations[stageIndex].has_value();
    }

    std::optional<StageSpeculation>
    get(size_t stageIndex) const
    {
        if (stageIndex >= stageSpeculations.size()) {
            throw std::out_of_range("stage index out of range");
        }
        return stageSpeculations[stageIndex];
    }

  private:
    std::vector<std::optional<StageSpeculation>> stageSpeculations;
};

struct ScheduledCompletion
{
    Cycles readyAt;
    size_t stageIndex;
    bool taken;
};

struct PipelineCorrection
{
    ThreadID tid;
    InstSeqNum seqNum;
    Addr pc;
    size_t stageIndex;
    bool taken;
};

class PipelineCompletionQueue
{
  public:
    void
    schedule(const Cycles &now, size_t stageIndex, const Cycles &latency,
             bool taken)
    {
        pendingCompletions.push_back(
            ScheduledCompletion{now + latency, stageIndex, taken});
    }

    std::vector<ScheduledCompletion>
    popReady(const Cycles &now)
    {
        auto ready_begin = std::partition(
            pendingCompletions.begin(), pendingCompletions.end(),
            [&now](const ScheduledCompletion &completion) {
                return completion.readyAt > now;
            });

        std::vector<ScheduledCompletion> ready(ready_begin,
                                               pendingCompletions.end());
        pendingCompletions.erase(ready_begin, pendingCompletions.end());

        std::sort(ready.begin(), ready.end(),
                  [](const ScheduledCompletion &lhs,
                     const ScheduledCompletion &rhs) {
                      if (rhs.readyAt > lhs.readyAt) {
                          return true;
                      }
                      if (lhs.readyAt > rhs.readyAt) {
                          return false;
                      }
                      return lhs.stageIndex < rhs.stageIndex;
                  });

        return ready;
    }

    bool
    empty() const
    {
        return pendingCompletions.empty();
    }

  private:
    std::vector<ScheduledCompletion> pendingCompletions;
};

class PipelineCorrectionQueue
{
  public:
    void
    push(const PipelineCorrection &correction)
    {
        pendingCorrections.push_back(correction);
    }

    bool
    empty() const
    {
        return pendingCorrections.empty();
    }

    size_t
    size() const
    {
        return pendingCorrections.size();
    }

    PipelineCorrection
    pop()
    {
        if (pendingCorrections.empty()) {
            throw std::logic_error("no correction is pending");
        }

        auto correction = pendingCorrections.front();
        pendingCorrections.erase(pendingCorrections.begin());
        return correction;
    }

    template <typename Predicate>
    void
    eraseIf(Predicate predicate)
    {
        pendingCorrections.erase(std::remove_if(pendingCorrections.begin(),
                                                pendingCorrections.end(),
                                                predicate),
                                 pendingCorrections.end());
    }

  private:
    std::vector<PipelineCorrection> pendingCorrections;
};

struct PipelinePredictionState;

class PipelineStageConfiguration
{
  public:
    explicit PipelineStageConfiguration(std::vector<Cycles> latencies);

    size_t numStages() const;
    Cycles stageLatency(size_t stageIndex) const;
    void enqueuePredictions(const Cycles &now,
                            const std::vector<bool> &predictions,
                            PipelinePredictionState &state) const;
    void applyReadyResults(const Cycles &now,
                           PipelinePredictionState &state) const;

  private:
    std::vector<Cycles> stageLatencies;
};

struct PipelinePredictionState
{
    ThreadID tid;
    InstSeqNum seqNum;
    Addr pc;
    int effectiveStage;
    bool hasEffectiveResult;
    bool effectiveTaken;
    bool correctionIssued;
    std::optional<PipelineCorrection> pendingCorrection;
    std::vector<StagePredictionState> stages;
    PipelineCompletionQueue completionQueue;

    PipelinePredictionState(size_t numStages, ThreadID thread_id,
                            InstSeqNum sequence_num, Addr branch_pc)
        : tid(thread_id),
          seqNum(sequence_num),
          pc(branch_pc),
          effectiveStage(-1),
          hasEffectiveResult(false),
          effectiveTaken(false),
          correctionIssued(false),
          stages(numStages)
    {
        if (numStages == 0) {
            throw std::invalid_argument(
                "PipelinePredictionState requires at least one stage");
        }
    }

    bool
    applyStageResult(size_t stageIndex, bool taken)
    {
        if (stageIndex >= stages.size()) {
            throw std::out_of_range("stage index out of range");
        }

        auto &stage = stages[stageIndex];
        stage.completed = true;
        stage.hasPrediction = true;
        stage.predTaken = taken;

        if (hasEffectiveResult && (stageIndex <= effectiveStage)) {
            return false;
        }

        const bool correction = hasEffectiveResult && effectiveTaken != taken;
        correctionIssued = correctionIssued || correction;
        if (correction) {
            pendingCorrection =
                PipelineCorrection{tid, seqNum, pc, stageIndex, taken};
        }
        effectiveStage = stageIndex;
        hasEffectiveResult = true;
        effectiveTaken = taken;
        return true;
    }

    bool
    hasPendingCorrection() const
    {
        return pendingCorrection.has_value();
    }

    PipelineCorrection
    takeCorrection()
    {
        if (!pendingCorrection.has_value()) {
            throw std::logic_error("no correction is pending");
        }

        auto correction = *pendingCorrection;
        pendingCorrection.reset();
        return correction;
    }

    void
    recordStageSpeculation(
        size_t stageIndex, bool taken, std::optional<Addr> target,
        StageTargetSource source = StageTargetSource::Unresolved)
    {
        if (stageIndex >= stages.size()) {
            throw std::out_of_range("stage index out of range");
        }

        auto &stage = stages[stageIndex];
        stage.speculativeTaken = taken;
        stage.speculativeTarget = target;
    }

    bool
    isComplete() const
    {
        return completionQueue.empty();
    }
};

class PipelinePredictionTable
{
  public:
    void
    insert(PipelinePredictionState state)
    {
        entries.push_back(std::move(state));
    }

    PipelinePredictionState *
    find(InstSeqNum seqNum)
    {
        for (auto &entry : entries) {
            if (entry.seqNum == seqNum) {
                return &entry;
            }
        }

        return nullptr;
    }

    void
    applyReadyResults(const Cycles &now, PipelineCorrectionQueue &corrections)
    {
        for (auto &entry : entries) {
            auto ready = entry.completionQueue.popReady(now);
            for (const auto &completion : ready) {
                entry.applyStageResult(completion.stageIndex,
                                       completion.taken);
                if (entry.hasPendingCorrection()) {
                    corrections.push(entry.takeCorrection());
                }
            }
        }

        entries.erase(std::remove_if(entries.begin(), entries.end(),
                                     [](const PipelinePredictionState &entry) {
                                         return entry.isComplete();
                                     }),
                      entries.end());
    }

    size_t
    size() const
    {
        return entries.size();
    }

    template <typename Predicate>
    void
    eraseIf(Predicate predicate)
    {
        entries.erase(
            std::remove_if(entries.begin(), entries.end(), predicate),
            entries.end());
    }

  private:
    std::vector<PipelinePredictionState> entries;
};

inline PipelineStageConfiguration::PipelineStageConfiguration(
    std::vector<Cycles> latencies)
    : stageLatencies(std::move(latencies))
{
    if (stageLatencies.empty()) {
        throw std::invalid_argument(
            "PipelineStageConfiguration requires at least one stage");
    }
}

inline size_t
PipelineStageConfiguration::numStages() const
{
    return stageLatencies.size();
}

inline Cycles
PipelineStageConfiguration::stageLatency(size_t stageIndex) const
{
    return stageLatencies.at(stageIndex);
}

inline void
PipelineStageConfiguration::enqueuePredictions(
    const Cycles &now, const std::vector<bool> &predictions,
    PipelinePredictionState &state) const
{
    if (predictions.size() != numStages()) {
        throw std::invalid_argument(
            "prediction count must match configured pipeline stages");
    }

    for (size_t stageIndex = 0; stageIndex < predictions.size();
         ++stageIndex) {
        state.completionQueue.schedule(now, stageIndex,
                                       stageLatency(stageIndex),
                                       predictions[stageIndex]);
    }
}

inline void
PipelineStageConfiguration::applyReadyResults(
    const Cycles &now, PipelinePredictionState &state) const
{
    auto ready = state.completionQueue.popReady(now);
    for (const auto &completion : ready) {
        state.applyStageResult(completion.stageIndex, completion.taken);
    }
}

} // namespace branch_prediction
} // namespace gem5

#endif // __CPU_PRED_PIPELINED_BPRED_STATE_HH__
