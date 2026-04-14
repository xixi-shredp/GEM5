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

#include <gtest/gtest.h>

#include "cpu/pred/pipelined_bpred_state.hh"

namespace gem5
{

namespace branch_prediction
{

TEST(PipelinedBPredUnitState, RejectsZeroStages)
{
    EXPECT_THROW(PipelinePredictionState(0, 0, 11, 0x1000),
                 std::invalid_argument);
}

TEST(PipelinedBPredUnitState, AllocatesPerStageBookkeeping)
{
    PipelinePredictionState state(3, 1, 17, 0x2000);

    EXPECT_EQ(state.tid, 1);
    EXPECT_EQ(state.seqNum, 17);
    EXPECT_EQ(state.pc, 0x2000);
    EXPECT_EQ(state.effectiveStage, -1);
    EXPECT_FALSE(state.correctionIssued);
    ASSERT_EQ(state.stages.size(), 3);

    for (const auto &stage : state.stages) {
        EXPECT_EQ(stage.bpHistory, nullptr);
        EXPECT_FALSE(stage.completed);
    }
}

TEST(PipelinedBPredUnitState, HigherPriorityStageCanOverride)
{
    PipelinePredictionState state(3, 0, 21, 0x3000);

    EXPECT_TRUE(state.applyStageResult(0, true));
    EXPECT_EQ(state.effectiveStage, 0);
    EXPECT_TRUE(state.effectiveTaken);
    EXPECT_FALSE(state.correctionIssued);

    EXPECT_TRUE(state.applyStageResult(2, false));
    EXPECT_EQ(state.effectiveStage, 2);
    EXPECT_FALSE(state.effectiveTaken);
    EXPECT_TRUE(state.correctionIssued);
}

TEST(PipelinedBPredUnitState, LowerPriorityStageCannotOverride)
{
    PipelinePredictionState state(3, 0, 22, 0x4000);

    EXPECT_TRUE(state.applyStageResult(2, false));
    EXPECT_EQ(state.effectiveStage, 2);
    EXPECT_FALSE(state.effectiveTaken);

    EXPECT_FALSE(state.applyStageResult(1, true));
    EXPECT_EQ(state.effectiveStage, 2);
    EXPECT_FALSE(state.effectiveTaken);
}

TEST(PipelinedBPredUnitState, ScheduledCompletionsArePoppedInReadyOrder)
{
    PipelineCompletionQueue queue;

    queue.schedule(Cycles(10), 2, Cycles(5), false);
    queue.schedule(Cycles(10), 0, Cycles(1), true);
    queue.schedule(Cycles(10), 1, Cycles(3), true);

    auto ready_early = queue.popReady(Cycles(10));
    EXPECT_TRUE(ready_early.empty());

    auto ready_first = queue.popReady(Cycles(11));
    ASSERT_EQ(ready_first.size(), 1);
    EXPECT_EQ(ready_first[0].stageIndex, 0);

    auto ready_second = queue.popReady(Cycles(13));
    ASSERT_EQ(ready_second.size(), 1);
    EXPECT_EQ(ready_second[0].stageIndex, 1);

    auto ready_last = queue.popReady(Cycles(15));
    ASSERT_EQ(ready_last.size(), 1);
    EXPECT_EQ(ready_last[0].stageIndex, 2);

    EXPECT_TRUE(queue.empty());
}

TEST(PipelinedBPredUnitState, SchedulesAndConsumesConfiguredStageLatencies)
{
    PipelineStageConfiguration config({Cycles(5), Cycles(1), Cycles(3)});
    PipelinePredictionState state(config.numStages(), 0, 23, 0x5000);

    config.enqueuePredictions(Cycles(10), {false, true, true}, state);

    EXPECT_EQ(config.numStages(), 3U);
    EXPECT_EQ(uint64_t(config.stageLatency(0)), 5U);
    EXPECT_EQ(uint64_t(config.stageLatency(1)), 1U);
    EXPECT_EQ(uint64_t(config.stageLatency(2)), 3U);

    config.applyReadyResults(Cycles(11), state);
    EXPECT_EQ(state.effectiveStage, 1);
    EXPECT_TRUE(state.effectiveTaken);

    config.applyReadyResults(Cycles(13), state);
    EXPECT_EQ(state.effectiveStage, 2);
    EXPECT_TRUE(state.effectiveTaken);

    config.applyReadyResults(Cycles(15), state);
    EXPECT_EQ(state.effectiveStage, 2);
    EXPECT_TRUE(state.effectiveTaken);
}

TEST(PipelinedBPredUnitState, LateDirectionFlipEmitsCorrectionEvent)
{
    PipelineStageConfiguration config({Cycles(1), Cycles(3)});
    PipelinePredictionState state(config.numStages(), 2, 31, 0x6000);

    config.enqueuePredictions(Cycles(10), {true, false}, state);

    config.applyReadyResults(Cycles(11), state);
    EXPECT_FALSE(state.hasPendingCorrection());

    config.applyReadyResults(Cycles(13), state);
    ASSERT_TRUE(state.hasPendingCorrection());

    const auto correction = state.takeCorrection();
    EXPECT_EQ(correction.tid, 2);
    EXPECT_EQ(correction.seqNum, 31);
    EXPECT_EQ(correction.pc, 0x6000U);
    EXPECT_EQ(correction.stageIndex, 1U);
    EXPECT_FALSE(correction.taken);
    EXPECT_FALSE(state.hasPendingCorrection());
}

TEST(PipelinedBPredUnitState, SameDirectionOverrideDoesNotEmitCorrectionEvent)
{
    PipelineStageConfiguration config({Cycles(1), Cycles(3)});
    PipelinePredictionState state(config.numStages(), 3, 32, 0x7000);

    config.enqueuePredictions(Cycles(20), {true, true}, state);
    config.applyReadyResults(Cycles(21), state);
    config.applyReadyResults(Cycles(23), state);

    EXPECT_EQ(state.effectiveStage, 1);
    EXPECT_TRUE(state.effectiveTaken);
    EXPECT_FALSE(state.hasPendingCorrection());
}

TEST(PipelinedBPredUnitState, CorrectionQueuePreservesArrivalOrder)
{
    PipelineCorrectionQueue queue;

    queue.push(PipelineCorrection{0, 41, 0x8000, 1, false});
    queue.push(PipelineCorrection{0, 42, 0x8040, 2, true});

    ASSERT_EQ(queue.size(), 2U);

    const auto first = queue.pop();
    EXPECT_EQ(first.seqNum, 41);
    EXPECT_EQ(first.pc, 0x8000U);
    EXPECT_FALSE(first.taken);

    const auto second = queue.pop();
    EXPECT_EQ(second.seqNum, 42);
    EXPECT_EQ(second.pc, 0x8040U);
    EXPECT_TRUE(second.taken);

    EXPECT_TRUE(queue.empty());
}

TEST(PipelinedBPredUnitState, PredictionTableFindsRegisteredState)
{
    PipelinePredictionTable table;

    table.insert(PipelinePredictionState(2, 0, 51, 0x9000));

    auto *state = table.find(51);
    ASSERT_NE(state, nullptr);
    EXPECT_EQ(state->pc, 0x9000U);
    EXPECT_EQ(state->seqNum, 51);
    EXPECT_EQ(table.find(52), nullptr);
}

TEST(PipelinedBPredUnitState, PredictionTableHarvestsCorrectionsAcrossEntries)
{
    PipelineStageConfiguration config({Cycles(1), Cycles(3)});
    PipelinePredictionTable table;
    PipelineCorrectionQueue corrections;

    auto first = PipelinePredictionState(config.numStages(), 0, 61, 0xa000);
    auto second = PipelinePredictionState(config.numStages(), 0, 62, 0xa040);
    config.enqueuePredictions(Cycles(10), {true, false}, first);
    config.enqueuePredictions(Cycles(10), {true, true}, second);
    table.insert(std::move(first));
    table.insert(std::move(second));

    table.applyReadyResults(Cycles(11), corrections);
    EXPECT_TRUE(corrections.empty());

    table.applyReadyResults(Cycles(13), corrections);
    ASSERT_EQ(corrections.size(), 1U);

    const auto correction = corrections.pop();
    EXPECT_EQ(correction.seqNum, 61);
    EXPECT_EQ(correction.pc, 0xa000U);
    EXPECT_FALSE(correction.taken);
    EXPECT_EQ(table.size(), 0U);
}

TEST(PipelinedBPredUnitState, PredictionTableRetiresCompletedEntries)
{
    PipelineStageConfiguration config({Cycles(1), Cycles(2)});
    PipelinePredictionTable table;
    PipelineCorrectionQueue corrections;

    auto state = PipelinePredictionState(config.numStages(), 0, 71, 0xb000);
    config.enqueuePredictions(Cycles(10), {true, true}, state);
    table.insert(std::move(state));

    EXPECT_EQ(table.size(), 1U);

    table.applyReadyResults(Cycles(11), corrections);
    EXPECT_EQ(table.size(), 1U);

    table.applyReadyResults(Cycles(12), corrections);
    EXPECT_EQ(table.size(), 0U);
    EXPECT_TRUE(corrections.empty());
}

TEST(PipelinedBPredUnitState, PredictionTableEraseIfRemovesMatchingEntries)
{
    PipelinePredictionTable table;

    table.insert(PipelinePredictionState(2, 0, 81, 0xc000));
    table.insert(PipelinePredictionState(2, 0, 82, 0xc040));
    table.insert(PipelinePredictionState(2, 0, 83, 0xc080));

    table.eraseIf([](const PipelinePredictionState &entry) {
        return entry.seqNum >= 82;
    });

    EXPECT_EQ(table.size(), 1U);
    ASSERT_NE(table.find(81), nullptr);
    EXPECT_EQ(table.find(82), nullptr);
    EXPECT_EQ(table.find(83), nullptr);
}

TEST(PipelinedBPredUnitState, CorrectionQueueEraseIfDropsStaleCorrections)
{
    PipelineCorrectionQueue queue;

    queue.push(PipelineCorrection{0, 91, 0xd000, 1, false});
    queue.push(PipelineCorrection{0, 92, 0xd040, 2, true});
    queue.push(PipelineCorrection{0, 93, 0xd080, 2, false});

    queue.eraseIf([](const PipelineCorrection &correction) {
        return correction.seqNum >= 92;
    });

    ASSERT_EQ(queue.size(), 1U);
    const auto remaining = queue.pop();
    EXPECT_EQ(remaining.seqNum, 91);
    EXPECT_FALSE(remaining.taken);
}

TEST(PipelinedBPredUnitState, StageHistoryStorageRecordsPerStagePointers)
{
    PipelineStageHistoryStorage storage(3);
    int stage_one = 1;
    int stage_two = 2;

    storage.record(1, &stage_one);
    storage.record(2, &stage_two);

    EXPECT_EQ(storage.size(), 3U);
    EXPECT_TRUE(storage.hasHistory(1));
    EXPECT_TRUE(storage.hasHistory(2));
    EXPECT_EQ(storage.get(1), &stage_one);
    EXPECT_EQ(storage.get(2), &stage_two);
}

TEST(PipelinedBPredUnitState, StageHistoryStorageTakeClearsOnlySelectedStage)
{
    PipelineStageHistoryStorage storage(3);
    int stage_one = 1;
    int stage_two = 2;

    storage.record(1, &stage_one);
    storage.record(2, &stage_two);

    EXPECT_EQ(storage.take(1), &stage_one);
    EXPECT_FALSE(storage.hasHistory(1));
    EXPECT_TRUE(storage.hasHistory(2));
    EXPECT_EQ(storage.take(2), &stage_two);
    EXPECT_TRUE(storage.empty());
}

TEST(PipelinedBPredUnitState, StageHistoryStorageResizeMakesStagesAddressable)
{
    PipelineStageHistoryStorage storage;

    storage.resize(4);

    EXPECT_EQ(storage.size(), 4U);
    for (size_t index = 0; index < storage.size(); ++index) {
        EXPECT_FALSE(storage.hasHistory(index));
        EXPECT_EQ(storage.get(index), nullptr);
    }
}

TEST(PipelinedBPredUnitState, StagePredictionStateCanTrackIndependentPath)
{
    StagePredictionState stage;

    stage.speculativeTaken = true;
    stage.speculativeTarget = 0x1234;

    EXPECT_TRUE(stage.speculativeTaken.has_value());
    EXPECT_TRUE(*stage.speculativeTaken);
    ASSERT_TRUE(stage.speculativeTarget.has_value());
    EXPECT_EQ(*stage.speculativeTarget, 0x1234U);
}

TEST(PipelinedBPredUnitState, PipelinePredictionStateRecordsPerStagePaths)
{
    PipelinePredictionState state(3, 0, 101, 0x1000);

    state.recordStageSpeculation(0, true, 0x2000);
    state.recordStageSpeculation(2, false, 0x1004);

    ASSERT_TRUE(state.stages[0].speculativeTaken.has_value());
    EXPECT_TRUE(*state.stages[0].speculativeTaken);
    ASSERT_TRUE(state.stages[0].speculativeTarget.has_value());
    EXPECT_EQ(*state.stages[0].speculativeTarget, 0x2000U);

    ASSERT_TRUE(state.stages[2].speculativeTaken.has_value());
    EXPECT_FALSE(*state.stages[2].speculativeTaken);
    ASSERT_TRUE(state.stages[2].speculativeTarget.has_value());
    EXPECT_EQ(*state.stages[2].speculativeTarget, 0x1004U);
}

TEST(PipelinedBPredUnitState,
     PipelinePredictionStateAllowsTakenWithoutResolvedTarget)
{
    PipelinePredictionState state(2, 0, 102, 0x1000);

    state.recordStageSpeculation(1, true, std::nullopt);

    ASSERT_TRUE(state.stages[1].speculativeTaken.has_value());
    EXPECT_TRUE(*state.stages[1].speculativeTaken);
    EXPECT_FALSE(state.stages[1].speculativeTarget.has_value());
}

TEST(PipelinedBPredUnitState, StageSpeculationStoragePersistsPerStagePath)
{
    PipelineStageSpeculationStorage storage(3);

    storage.record(1, true, 0x3000, StageTargetSource::FrontendTarget);
    storage.record(2, false, 0x1004, StageTargetSource::Fallthrough);

    ASSERT_TRUE(storage.hasSpeculation(1));
    ASSERT_TRUE(storage.hasSpeculation(2));

    const auto stage_one = storage.get(1);
    ASSERT_TRUE(stage_one.has_value());
    EXPECT_TRUE(stage_one->taken);
    EXPECT_EQ(stage_one->target, 0x3000U);
    EXPECT_EQ(stage_one->targetSource, StageTargetSource::FrontendTarget);

    const auto stage_two = storage.get(2);
    ASSERT_TRUE(stage_two.has_value());
    EXPECT_FALSE(stage_two->taken);
    EXPECT_EQ(stage_two->target, 0x1004U);
    EXPECT_EQ(stage_two->targetSource, StageTargetSource::Fallthrough);
}

TEST(PipelinedBPredUnitState, StageSpeculationCanRepresentTakenWithoutTarget)
{
    StageSpeculation speculation{true, std::nullopt,
                                 StageTargetSource::Unresolved};

    EXPECT_TRUE(speculation.taken);
    EXPECT_FALSE(speculation.target.has_value());
    EXPECT_EQ(speculation.targetSource, StageTargetSource::Unresolved);
}

TEST(PipelinedBPredUnitState, StageSpeculationStoragePreservesUnresolvedTarget)
{
    PipelineStageSpeculationStorage storage(2);

    storage.record(1, true, std::nullopt);

    const auto stage = storage.get(1);
    ASSERT_TRUE(stage.has_value());
    EXPECT_TRUE(stage->taken);
    EXPECT_FALSE(stage->target.has_value());
    EXPECT_EQ(stage->targetSource, StageTargetSource::Unresolved);
}

TEST(PipelinedBPredUnitState, StageHistoryUpdateInputPrefersResolvedStagePath)
{
    const auto input = resolveStageHistoryUpdateInput(
        StageSpeculation{false, 0x1010}, true, 0x2020);

    EXPECT_FALSE(input.taken);
    EXPECT_EQ(input.target, 0x1010U);
    EXPECT_FALSE(input.usedFallbackTarget);
}

TEST(PipelinedBPredUnitState, StageHistoryUpdateInputFallsBackOnlyOnTarget)
{
    const auto input = resolveStageHistoryUpdateInput(
        StageSpeculation{true, std::nullopt}, false, 0x2020);

    EXPECT_TRUE(input.taken);
    EXPECT_EQ(input.target, 0x2020U);
    EXPECT_TRUE(input.usedFallbackTarget);
}

TEST(PipelinedBPredUnitState,
     StageSpeculativeTargetUsesFallthroughWhenNotTaken)
{
    const auto target = resolveStageSpeculativeTarget(
        false, false, std::nullopt, std::nullopt, 0x1004);

    ASSERT_TRUE(target.target.has_value());
    EXPECT_EQ(*target.target, 0x1004U);
    EXPECT_EQ(target.source, StageTargetSource::Fallthrough);
}

TEST(PipelinedBPredUnitState,
     StageSpeculativeTargetPrefersFrontendTargetWhenAvailable)
{
    const auto target =
        resolveStageSpeculativeTarget(true, true, 0x3000, 0x2000, 0x1004);

    ASSERT_TRUE(target.target.has_value());
    EXPECT_EQ(*target.target, 0x3000U);
    EXPECT_EQ(target.source, StageTargetSource::FrontendTarget);
}

TEST(PipelinedBPredUnitState,
     StageSpeculativeTargetFallsBackToBtbWhenFrontendDidNotTake)
{
    const auto target = resolveStageSpeculativeTarget(
        true, false, std::nullopt, 0x2000, 0x1004);

    ASSERT_TRUE(target.target.has_value());
    EXPECT_EQ(*target.target, 0x2000U);
    EXPECT_EQ(target.source, StageTargetSource::BtbTarget);
}

TEST(PipelinedBPredUnitState,
     StageSpeculativeTargetRemainsUnresolvedWithoutHit)
{
    const auto target = resolveStageSpeculativeTarget(
        true, false, std::nullopt, std::nullopt, 0x1004);

    EXPECT_FALSE(target.target.has_value());
    EXPECT_EQ(target.source, StageTargetSource::Unresolved);
}

} // namespace branch_prediction
} // namespace gem5
