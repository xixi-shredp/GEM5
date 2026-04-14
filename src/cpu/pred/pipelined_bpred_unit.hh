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

#ifndef __CPU_PRED_PIPELINED_BPRED_UNIT_HH__
#define __CPU_PRED_PIPELINED_BPRED_UNIT_HH__

#include <vector>

#include "cpu/pred/bpred_unit.hh"
#include "cpu/pred/conditional.hh"
#include "cpu/pred/pipelined_bpred_state.hh"
#include "params/PipelinedBPredUnit.hh"

namespace gem5
{

namespace branch_prediction
{

class PipelinedBPredUnit : public BPredUnit
{
  public:
    explicit PipelinedBPredUnit(const PipelinedBPredUnitParams &params);

    bool predict(const StaticInstPtr &inst, const InstSeqNum &seqNum,
                 PCStateBase &pc, ThreadID tid,
                 PredictorHistory *&bpu_history) override;

    size_t
    numStages() const
    {
        return stagePredictors.size();
    }

    Cycles
    stageLatency(size_t stageIndex) const
    {
        return stageLatencies.at(stageIndex);
    }

    PipelinePredictionState
    makePipelineState(ThreadID tid, InstSeqNum seqNum, Addr pc) const
    {
        return PipelinePredictionState(numStages(), tid, seqNum, pc);
    }

    void enqueuePredictions(Cycles now, const std::vector<bool> &predictions,
                            PipelinePredictionState &state) const;

    void applyReadyResults(Cycles now, PipelinePredictionState &state);

    std::optional<PipelineCorrection>
    takeFrontendCorrection(ThreadID tid) override;

    void tick(ThreadID tid) override;

  private:
    void trackPrediction(ThreadID tid, InstSeqNum seqNum, Addr pc,
                         const std::vector<bool> &predictions);
    void commitAdditionalHistories(ThreadID tid,
                                   PredictorHistory *history) override;
    void squashAdditionalHistories(ThreadID tid,
                                   PredictorHistory *history) override;
    void recoverAdditionalHistories(ThreadID tid, PredictorHistory *history,
                                    bool actually_taken,
                                    const PCStateBase &corr_target) override;
    void commitResolvedThrough(const InstSeqNum &done_sn,
                               ThreadID tid) override;
    void squashResolvedYoungerThan(const InstSeqNum &squashed_sn,
                                   ThreadID tid) override;
    void squashResolvedAt(const InstSeqNum &squashed_sn,
                          ThreadID tid) override;

    std::vector<ConditionalPredictor *> stagePredictors;
    std::vector<Cycles> stageLatencies;
    std::vector<Cycles> pipelineCycles;
    std::vector<PipelinePredictionTable> predictionTables;
    std::vector<PipelineCorrectionQueue> correctionQueues;
};

} // namespace branch_prediction
} // namespace gem5

#endif // __CPU_PRED_PIPELINED_BPRED_UNIT_HH__
