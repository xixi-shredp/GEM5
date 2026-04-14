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

#ifndef __CPU_O3_BAC_REDIRECT_POLICY_HH__
#define __CPU_O3_BAC_REDIRECT_POLICY_HH__

#include <optional>

#include "base/types.hh"
#include "cpu/pred/pipelined_bpred_state.hh"

namespace gem5
{

namespace o3
{

struct FrontendRedirectSignals
{
    bool commitSquash = false;
    bool commitUpdate = false;
    bool decodeSquash = false;
    bool fetchSquash = false;
};

enum class PredictorCorrectionDisposition
{
    DropStale,
    RejectMissingTarget,
    DeferUnrevisable,
    Apply,
};

struct PredictorCorrectionDecision
{
    bool triggerRedirect = false;
    bool keepPending = false;
};

struct PredictorCorrectionTargetPlan
{
    bool requiresBtbTarget = false;
    std::optional<Addr> fallthroughTarget;
};

class PredictorCorrectionBuffer
{
  public:
    bool
    tryFill(const branch_prediction::PipelineCorrection &correction)
    {
        if (pendingCorrection.has_value()) {
            return false;
        }

        pendingCorrection = correction;
        return true;
    }

    void
    defer()
    {}

    void
    resolve()
    {
        pendingCorrection.reset();
    }

    const std::optional<branch_prediction::PipelineCorrection> &
    current() const
    {
        return pendingCorrection;
    }

  private:
    std::optional<branch_prediction::PipelineCorrection> pendingCorrection;
};

class BACRedirectPolicy
{
  public:
    static bool
    checkPredictorCorrectionFirst(const FrontendRedirectSignals &signals)
    {
        return !(signals.commitSquash || signals.decodeSquash ||
                 signals.fetchSquash);
    }

    static PredictorCorrectionDisposition
    decidePredictorCorrection(bool has_ft_entry, bool has_target,
                              bool revisable)
    {
        if (!has_ft_entry) {
            return PredictorCorrectionDisposition::DropStale;
        }
        if (!has_target) {
            return PredictorCorrectionDisposition::RejectMissingTarget;
        }
        if (!revisable) {
            return PredictorCorrectionDisposition::DeferUnrevisable;
        }

        return PredictorCorrectionDisposition::Apply;
    }

    static PredictorCorrectionDecision
    finalizePredictorCorrection(PredictorCorrectionBuffer &buffer,
                                PredictorCorrectionDisposition disposition)
    {
        switch (disposition) {
            case PredictorCorrectionDisposition::DropStale:
            case PredictorCorrectionDisposition::RejectMissingTarget:
                buffer.resolve();
                return {.triggerRedirect = false, .keepPending = false};
            case PredictorCorrectionDisposition::DeferUnrevisable:
                buffer.defer();
                return {.triggerRedirect = false, .keepPending = true};
            case PredictorCorrectionDisposition::Apply:
                buffer.resolve();
                return {.triggerRedirect = true, .keepPending = false};
        }

        return {};
    }

    static PredictorCorrectionTargetPlan
    planPredictorCorrectionTarget(bool taken, Addr endAddr, unsigned instSize)
    {
        if (taken) {
            return {.requiresBtbTarget = true,
                    .fallthroughTarget = std::nullopt};
        }

        return {
            .requiresBtbTarget = false,
            .fallthroughTarget = endAddr + instSize,
        };
    }
};

} // namespace o3
} // namespace gem5

#endif // __CPU_O3_BAC_REDIRECT_POLICY_HH__
