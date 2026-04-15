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

#include "cpu/pred/pipelined_bpred_unit.hh"

#include <memory>

#include "arch/generic/pcstate.hh"
#include "base/logging.hh"

namespace gem5
{

namespace branch_prediction
{

PipelinedBPredUnit::PipelinedBPredUnit(const PipelinedBPredUnitParams &params)
    : BPredUnit(params),
      stagePredictors(params.stagePredictors),
      stageLatencies(params.stageLatencies),
      pipelineCycles(numThreads, Cycles(0)),
      predictionTables(numThreads),
      correctionQueues(numThreads)
{
    fatal_if(stagePredictors.empty(),
             "PipelinedBPredUnit requires at least one stage predictor");
    fatal_if(stagePredictors.front() != cPred,
             "PipelinedBPredUnit stagePredictors[0] must match "
             "conditionalBranchPred while later-stage speculative history "
             "is not yet wired into commit/squash");
    fatal_if(stageLatencies.front() != Cycles(0),
             "PipelinedBPredUnit stageLatencies[0] must be 0 so the "
             "externally visible prediction remains single-cycle");
    for (size_t stageIndex = 1; stageIndex < stageLatencies.size();
         ++stageIndex) {
        fatal_if(stageLatencies[stageIndex] < stageLatencies[stageIndex - 1],
                 "PipelinedBPredUnit stageLatencies must be monotonically "
                 "nondecreasing from fastest to slowest stage");
    }
}

bool
PipelinedBPredUnit::predict(const StaticInstPtr &inst,
                            const InstSeqNum &seqNum, PCStateBase &pc,
                            ThreadID tid, PredictorHistory *&hist)
{
    assert(hist == nullptr);

    const BranchType brType = getBranchType(inst);
    hist = new PredictorHistory(tid, seqNum, pc.instAddr(), inst);

    stats.lookups[tid][brType]++;
    ppBranches->notify(1);

    stats.BTBLookups++;
    const PCStateBase *btb_target = btb->lookup(tid, pc.instAddr(), brType);
    if (btb_target) {
        stats.BTBHits++;
        hist->btbHit = true;
    }

    std::vector<bool> stagePredictions;
    stagePredictions.reserve(stagePredictors.size());

    const bool directed_taken_hint =
        !hist->uncond && hist->btbHit &&
        btb->lookupDirectionHint(tid, pc.instAddr(), brType);

    if (hist->uncond) {
        hist->condPred = true;
        hist->stagedBpHistory.resize(stagePredictors.size());
        hist->stagedSpeculation.resize(stagePredictors.size());
        stagePredictions.assign(stagePredictors.size(), true);
    } else if (directed_taken_hint) {
        ++stats.condPredicted;
        hist->condPred = true;
        hist->stagedBpHistory.resize(stagePredictors.size());
        hist->stagedSpeculation.resize(stagePredictors.size());
        stagePredictions.assign(stagePredictors.size(), true);

        for (size_t stageIndex = 0; stageIndex < stagePredictors.size();
             ++stageIndex) {
            void *stage_history = nullptr;
            stagePredictors[stageIndex]->branchPlaceholder(
                tid, pc.instAddr(), false, stage_history);

            if (stageIndex == 0) {
                hist->bpHistory = stage_history;
            } else if (stage_history != nullptr) {
                hist->stagedBpHistory.record(stageIndex, stage_history);
            }
        }

        ++stats.condPredictedTaken;
    } else {
        ++stats.condPredicted;
        hist->stagedBpHistory.resize(stagePredictors.size());
        hist->stagedSpeculation.resize(stagePredictors.size());

        for (size_t stageIndex = 0; stageIndex < stagePredictors.size();
             ++stageIndex) {
            void *stage_history = nullptr;
            const bool pred = stagePredictors[stageIndex]->lookup(
                tid, pc.instAddr(), stage_history);
            stagePredictions.push_back(pred);

            if (stageIndex == 0) {
                hist->condPred = pred;
                hist->bpHistory = stage_history;
            } else if (stage_history != nullptr) {
                hist->stagedBpHistory.record(stageIndex, stage_history);
            }
        }

        if (hist->condPred) {
            ++stats.condPredictedTaken;
        }
    }
    hist->predTaken = hist->condPred;

    if (!stagePredictions.empty()) {
        trackPrediction(tid, seqNum, pc.instAddr(), stagePredictions);
    }

    hist->targetProvider = enums::TargetProvider::NoTarget;

    if (btb_target && hist->predTaken) {
        hist->targetProvider = enums::TargetProvider::BTB;
        set(hist->target, btb_target);
    }

    const bool branch_detected = (hist->btbHit || !requiresBTBHit);

    if (ras && branch_detected) {
        if (inst->isCall()) {
            auto return_addr = inst->buildRetPC(pc, pc);
            ras->push(tid, *return_addr, hist->rasHistory);
        } else if (inst->isReturn()) {
            const PCStateBase *return_addr = ras->pop(tid, hist->rasHistory);
            if (return_addr) {
                set(hist->target, *return_addr);
                hist->targetProvider = enums::TargetProvider::RAS;
            }
        }
    }

    if (iPred && hist->predTaken && branch_detected &&
        inst->isIndirectCtrl() && !inst->isReturn()) {
        ++stats.indirectLookups;

        std::unique_ptr<const PCStateBase> itarget(
            iPred->lookup(tid, seqNum, pc.instAddr(), hist->indirectHistory));

        if (itarget) {
            ++stats.indirectHits;
            hist->targetProvider = enums::TargetProvider::Indirect;
            set(hist->target, *itarget);
        } else {
            ++stats.indirectMisses;
        }
    }

    if (hist->targetProvider == enums::TargetProvider::NoTarget) {
        set(hist->target, pc);
        inst->advancePC(*hist->target);
        hist->predTaken = false;
    }
    stats.targetProvider[tid][hist->targetProvider]++;

    hist->actuallyTaken = hist->predTaken;
    set(pc, *hist->target);

    std::optional<Addr> frontend_target;
    if (hist->predTaken &&
        hist->targetProvider != enums::TargetProvider::NoTarget) {
        frontend_target = hist->target->instAddr();
    }

    std::optional<Addr> btb_taken_target;
    if (btb_target != nullptr) {
        btb_taken_target = btb_target->instAddr();
    }

    if (auto *state = predictionTables.at(tid).find(seqNum)) {
        for (size_t stageIndex = 0; stageIndex < stagePredictions.size();
             ++stageIndex) {
            const bool stage_taken = stagePredictions[stageIndex];
            const auto stage_target = resolveStageSpeculativeTarget(
                stage_taken, hist->predTaken, frontend_target,
                btb_taken_target, hist->pc + hist->inst->size());

            state->recordStageSpeculation(stageIndex, stage_taken,
                                          stage_target.target,
                                          stage_target.source);
            hist->stagedSpeculation.record(stageIndex, stage_taken,
                                           stage_target.target,
                                           stage_target.source);
        }
    }

    cPred->updateHistories(tid, hist->pc, hist->uncond, hist->predTaken,
                           hist->target->instAddr(), hist->inst,
                           hist->bpHistory);
    for (size_t stageIndex = 1; stageIndex < stagePredictors.size();
         ++stageIndex) {
        void *stage_history = hist->stagedBpHistory.get(stageIndex);
        if (stage_history == nullptr) {
            continue;
        }

        const auto stage_input = resolveStageHistoryUpdateInput(
            hist->stagedSpeculation.get(stageIndex), hist->predTaken,
            hist->target->instAddr());
        stagePredictors[stageIndex]->updateHistories(
            tid, hist->pc, hist->uncond, stage_input.taken, stage_input.target,
            hist->inst, stage_history);
    }

    if (iPred) {
        iPred->update(tid, seqNum, hist->pc, false, hist->predTaken,
                      *hist->target, brType, hist->indirectHistory);
    }

    return hist->predTaken;
}

void
PipelinedBPredUnit::commitAdditionalHistories(ThreadID tid,
                                              PredictorHistory *history)
{
    for (size_t stageIndex = 1; stageIndex < stagePredictors.size();
         ++stageIndex) {
        void *stage_history = history->stagedBpHistory.take(stageIndex);
        if (stage_history == nullptr) {
            continue;
        }

        stagePredictors[stageIndex]->update(
            tid, history->pc, history->actuallyTaken, stage_history, false,
            history->inst, history->target->instAddr());
    }
}

void
PipelinedBPredUnit::squashAdditionalHistories(ThreadID tid,
                                              PredictorHistory *history)
{
    for (size_t stageIndex = 1; stageIndex < stagePredictors.size();
         ++stageIndex) {
        void *stage_history = history->stagedBpHistory.take(stageIndex);
        if (stage_history == nullptr) {
            continue;
        }

        stagePredictors[stageIndex]->squash(tid, stage_history);
    }
}

void
PipelinedBPredUnit::recoverAdditionalHistories(ThreadID tid,
                                               PredictorHistory *history,
                                               bool actually_taken,
                                               const PCStateBase &corr_target)
{
    for (size_t stageIndex = 1; stageIndex < stagePredictors.size();
         ++stageIndex) {
        void *stage_history = history->stagedBpHistory.get(stageIndex);
        if (stage_history == nullptr) {
            continue;
        }

        const auto speculation = history->stagedSpeculation.get(stageIndex);
        if (speculation.has_value() && speculation->taken == actually_taken &&
            speculation->target.has_value() &&
            *speculation->target == corr_target.instAddr()) {
            continue;
        }

        stagePredictors[stageIndex]->update(tid, history->pc, actually_taken,
                                            stage_history, true, history->inst,
                                            corr_target.instAddr());
    }
}

void
PipelinedBPredUnit::trackPrediction(ThreadID tid, InstSeqNum seqNum, Addr pc,
                                    const std::vector<bool> &predictions)
{
    auto state = makePipelineState(tid, seqNum, pc);
    enqueuePredictions(pipelineCycles.at(tid), predictions, state);
    predictionTables.at(tid).insert(std::move(state));
}

void
PipelinedBPredUnit::enqueuePredictions(Cycles now,
                                       const std::vector<bool> &predictions,
                                       PipelinePredictionState &state) const
{
    if (predictions.size() != numStages()) {
        throw std::invalid_argument(
            "prediction count must match configured pipeline stages");
    }

    for (size_t stageIndex = 0; stageIndex < predictions.size();
         ++stageIndex) {
        state.completionQueue.schedule(now, stageIndex,
                                       stageLatencies.at(stageIndex),
                                       predictions[stageIndex]);
    }
}

void
PipelinedBPredUnit::applyReadyResults(Cycles now,
                                      PipelinePredictionState &state)
{
    auto ready = state.completionQueue.popReady(now);
    for (const auto &completion : ready) {
        state.applyStageResult(completion.stageIndex, completion.taken);
        if (state.hasPendingCorrection()) {
            correctionQueues.at(state.tid).push(state.takeCorrection());
        }
    }
}

std::optional<PipelineCorrection>
PipelinedBPredUnit::takeFrontendCorrection(ThreadID tid)
{
    auto &queue = correctionQueues.at(tid);
    if (queue.empty()) {
        return std::nullopt;
    }

    return queue.pop();
}

void
PipelinedBPredUnit::tick(ThreadID tid)
{
    pipelineCycles.at(tid) += Cycles(1);
    predictionTables.at(tid).applyReadyResults(pipelineCycles.at(tid),
                                               correctionQueues.at(tid));
}

void
PipelinedBPredUnit::commitResolvedThrough(const InstSeqNum &done_sn,
                                          ThreadID tid)
{
    predictionTables.at(tid).eraseIf(
        [&done_sn](const PipelinePredictionState &entry) {
            return entry.seqNum <= done_sn;
        });
    correctionQueues.at(tid).eraseIf(
        [&done_sn](const PipelineCorrection &correction) {
            return correction.seqNum <= done_sn;
        });
}

void
PipelinedBPredUnit::squashResolvedYoungerThan(const InstSeqNum &squashed_sn,
                                              ThreadID tid)
{
    predictionTables.at(tid).eraseIf(
        [&squashed_sn](const PipelinePredictionState &entry) {
            return entry.seqNum > squashed_sn;
        });
    correctionQueues.at(tid).eraseIf(
        [&squashed_sn](const PipelineCorrection &correction) {
            return correction.seqNum > squashed_sn;
        });
}

void
PipelinedBPredUnit::squashResolvedAt(const InstSeqNum &squashed_sn,
                                     ThreadID tid)
{
    predictionTables.at(tid).eraseIf(
        [&squashed_sn](const PipelinePredictionState &entry) {
            return entry.seqNum == squashed_sn;
        });
    correctionQueues.at(tid).eraseIf(
        [&squashed_sn](const PipelineCorrection &correction) {
            return correction.seqNum == squashed_sn;
        });
}

} // namespace branch_prediction
} // namespace gem5
