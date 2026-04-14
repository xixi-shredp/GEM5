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

#include "cpu/o3/fetch_target_pred_state.hh"

namespace gem5
{

namespace o3
{

TEST(FetchTargetPredictionState, FinalizeRecordsInitialPredictorStage)
{
    FetchTargetPredictionState state;

    state.initialize(0);

    EXPECT_EQ(state.stage(), 0U);
    EXPECT_FALSE(state.corrected());
}

TEST(FetchTargetPredictionState, RevisionUpdatesEffectivePredictionMetadata)
{
    FetchTargetPredictionState state;

    state.initialize(0);
    state.revise(true, 2);

    EXPECT_EQ(state.stage(), 2U);
    EXPECT_TRUE(state.corrected());
}

TEST(FetchTargetPredictionState, SamePredictionOnlyAdvancesProviderStage)
{
    FetchTargetPredictionState state;

    state.initialize(0);
    state.revise(false, 2);

    EXPECT_EQ(state.stage(), 2U);
    EXPECT_FALSE(state.corrected());
}

TEST(FetchTargetRevisionPolicy, ReadyQueueAllowsAnyInFlightRevision)
{
    FetchTargetRevisionPolicy policy(FetchTargetRevisionPolicy::Ready);

    EXPECT_TRUE(policy.allowsRevision(false));
    EXPECT_TRUE(policy.allowsRevision(true));
}

TEST(FetchTargetRevisionPolicy, InvalidQueueRejectsAllRevisions)
{
    FetchTargetRevisionPolicy policy(FetchTargetRevisionPolicy::Invalid);

    EXPECT_FALSE(policy.allowsRevision(false));
    EXPECT_FALSE(policy.allowsRevision(true));
}

TEST(FetchTargetRevisionPolicy, LockedQueueOnlyAllowsHeadRevision)
{
    FetchTargetRevisionPolicy policy(FetchTargetRevisionPolicy::Locked);

    EXPECT_TRUE(policy.allowsRevision(true));
    EXPECT_FALSE(policy.allowsRevision(false));
}

} // namespace o3
} // namespace gem5
