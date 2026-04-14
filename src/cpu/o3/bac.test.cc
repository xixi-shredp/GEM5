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

#include "cpu/o3/bac_redirect_policy.hh"

namespace gem5
{

namespace o3
{

TEST(BACRedirectPolicy, AllowsPredictorCorrectionWithoutExternalSignals)
{
    FrontendRedirectSignals signals;

    EXPECT_TRUE(BACRedirectPolicy::checkPredictorCorrectionFirst(signals));
}

TEST(BACRedirectPolicy, CommitUpdateTakesPriorityOverPredictorCorrection)
{
    FrontendRedirectSignals signals;
    signals.commitUpdate = true;

    EXPECT_TRUE(BACRedirectPolicy::checkPredictorCorrectionFirst(signals));
}

TEST(BACRedirectPolicy, SquashesTakePriorityOverPredictorCorrection)
{
    FrontendRedirectSignals commit_signals;
    commit_signals.commitSquash = true;
    EXPECT_FALSE(
        BACRedirectPolicy::checkPredictorCorrectionFirst(commit_signals));

    FrontendRedirectSignals decode_signals;
    decode_signals.decodeSquash = true;
    EXPECT_FALSE(
        BACRedirectPolicy::checkPredictorCorrectionFirst(decode_signals));

    FrontendRedirectSignals fetch_signals;
    fetch_signals.fetchSquash = true;
    EXPECT_FALSE(
        BACRedirectPolicy::checkPredictorCorrectionFirst(fetch_signals));
}

TEST(BACRedirectPolicy, DropsStalePredictorCorrectionBeforeTargetLookup)
{
    const auto disposition =
        BACRedirectPolicy::decidePredictorCorrection(false, true, true);

    EXPECT_EQ(disposition, PredictorCorrectionDisposition::DropStale);
}

TEST(BACRedirectPolicy, RejectsTakenCorrectionWithoutTarget)
{
    const auto disposition =
        BACRedirectPolicy::decidePredictorCorrection(true, false, true);

    EXPECT_EQ(disposition,
              PredictorCorrectionDisposition::RejectMissingTarget);
}

TEST(BACRedirectPolicy, RejectsUnrevisableFtqEntry)
{
    const auto disposition =
        BACRedirectPolicy::decidePredictorCorrection(true, true, false);

    EXPECT_EQ(disposition, PredictorCorrectionDisposition::DeferUnrevisable);
}

TEST(BACRedirectPolicy, AppliesCorrectionWhenAllPreconditionsHold)
{
    const auto disposition =
        BACRedirectPolicy::decidePredictorCorrection(true, true, true);

    EXPECT_EQ(disposition, PredictorCorrectionDisposition::Apply);
}

TEST(BACRedirectPolicy, DeferredCorrectionStaysPendingUntilResolved)
{
    PredictorCorrectionBuffer buffer;
    const branch_prediction::PipelineCorrection correction{0, 41, 0x1000, 1,
                                                           false};

    EXPECT_FALSE(buffer.current().has_value());

    EXPECT_TRUE(buffer.tryFill(correction));
    ASSERT_TRUE(buffer.current().has_value());
    EXPECT_EQ(buffer.current()->seqNum, 41);

    buffer.defer();
    ASSERT_TRUE(buffer.current().has_value());
    EXPECT_EQ(buffer.current()->seqNum, 41);

    buffer.resolve();
    EXPECT_FALSE(buffer.current().has_value());
}

TEST(BACRedirectPolicy, DeferredCorrectionPreventsYoungerReplacement)
{
    PredictorCorrectionBuffer buffer;
    const branch_prediction::PipelineCorrection older{0, 41, 0x1000, 1, false};
    const branch_prediction::PipelineCorrection younger{0, 42, 0x1040, 2,
                                                        true};

    EXPECT_TRUE(buffer.tryFill(older));
    EXPECT_FALSE(buffer.tryFill(younger));

    ASSERT_TRUE(buffer.current().has_value());
    EXPECT_EQ(buffer.current()->seqNum, 41);
}

TEST(BACRedirectPolicy, DeferredCorrectionRetriesAndEventuallyApplies)
{
    PredictorCorrectionBuffer buffer;
    const branch_prediction::PipelineCorrection correction{0, 51, 0x1100, 2,
                                                           true};

    ASSERT_TRUE(buffer.tryFill(correction));

    const auto deferred = BACRedirectPolicy::finalizePredictorCorrection(
        buffer, PredictorCorrectionDisposition::DeferUnrevisable);
    EXPECT_FALSE(deferred.triggerRedirect);
    EXPECT_TRUE(deferred.keepPending);
    ASSERT_TRUE(buffer.current().has_value());
    EXPECT_EQ(buffer.current()->seqNum, 51);

    const auto applied = BACRedirectPolicy::finalizePredictorCorrection(
        buffer, PredictorCorrectionDisposition::Apply);
    EXPECT_TRUE(applied.triggerRedirect);
    EXPECT_FALSE(applied.keepPending);
    EXPECT_FALSE(buffer.current().has_value());
}

TEST(BACRedirectPolicy, StaleCorrectionIsDroppedWithoutRedirect)
{
    PredictorCorrectionBuffer buffer;
    const branch_prediction::PipelineCorrection correction{0, 52, 0x1140, 1,
                                                           false};

    ASSERT_TRUE(buffer.tryFill(correction));

    const auto dropped = BACRedirectPolicy::finalizePredictorCorrection(
        buffer, PredictorCorrectionDisposition::DropStale);
    EXPECT_FALSE(dropped.triggerRedirect);
    EXPECT_FALSE(dropped.keepPending);
    EXPECT_FALSE(buffer.current().has_value());
}

TEST(BACRedirectPolicy, TakenCorrectionRequiresBtbTargetLookup)
{
    const auto plan =
        BACRedirectPolicy::planPredictorCorrectionTarget(true, 0x2000, 4);

    EXPECT_TRUE(plan.requiresBtbTarget);
    EXPECT_FALSE(plan.fallthroughTarget.has_value());
}

TEST(BACRedirectPolicy, NotTakenCorrectionUsesSequentialFallthrough)
{
    const auto plan =
        BACRedirectPolicy::planPredictorCorrectionTarget(false, 0x2000, 4);

    EXPECT_FALSE(plan.requiresBtbTarget);
    ASSERT_TRUE(plan.fallthroughTarget.has_value());
    EXPECT_EQ(*plan.fallthroughTarget, 0x2004U);
}

} // namespace o3
} // namespace gem5
