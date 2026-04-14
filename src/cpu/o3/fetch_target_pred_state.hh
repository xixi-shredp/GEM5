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

#ifndef __CPU_O3_FETCH_TARGET_PRED_STATE_HH__
#define __CPU_O3_FETCH_TARGET_PRED_STATE_HH__

#include <limits>

#include "base/types.hh"

namespace gem5
{

namespace o3
{

class FetchTargetPredictionState
{
  public:
    void
    initialize(size_t predictor_stage)
    {
        stageValue = predictor_stage;
        correctedValue = false;
    }

    void
    revise(bool prediction_changed, size_t predictor_stage)
    {
        stageValue = predictor_stage;
        correctedValue = correctedValue || prediction_changed;
    }

    size_t
    stage() const
    {
        return stageValue;
    }

    bool
    corrected() const
    {
        return correctedValue;
    }

  private:
    size_t stageValue = std::numeric_limits<size_t>::max();
    bool correctedValue = false;
};

class FetchTargetRevisionPolicy
{
  public:
    enum QueueState
    {
        Ready,
        Locked,
        Invalid,
    };

    explicit FetchTargetRevisionPolicy(QueueState queue_state)
        : state(queue_state)
    {}

    bool
    allowsRevision(bool is_head) const
    {
        switch (state) {
            case Ready:
                return true;
            case Locked:
                return is_head;
            case Invalid:
                return false;
        }

        return false;
    }

  private:
    QueueState state;
};

} // namespace o3
} // namespace gem5

#endif // __CPU_O3_FETCH_TARGET_PRED_STATE_HH__
