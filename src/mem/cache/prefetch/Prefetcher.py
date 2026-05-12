# Copyright (c) 2012, 2014, 2019, 2022-2025 Arm Limited
# Copyright (c) 2023 The University of Edinburgh
# All rights reserved.
#
# The license below extends only to copyright in the software and shall
# not be construed as granting a license to any other intellectual
# property including but not limited to intellectual property relating
# to a hardware implementation of the functionality of the software
# licensed hereunder.  You may use the software subject to the license
# terms below provided that you ensure that this notice is replicated
# unmodified and in its entirety in all distributions of the software,
# modified or unmodified, in source code or in binary form.
#
# Copyright (c) 2005 The Regents of The University of Michigan
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met: redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer;
# redistributions in binary form must reproduce the above copyright
# notice, this list of conditions and the following disclaimer in the
# documentation and/or other materials provided with the distribution;
# neither the name of the copyright holders nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

from m5.citations import add_citation
from m5.objects.ClockedObject import ClockedObject
from m5.objects.IndexingPolicies import *
from m5.objects.PartitioningPolicies import *
from m5.objects.ReplacementPolicies import *
from m5.objects.Tags import *
from m5.params import *
from m5.proxy import *
from m5.SimObject import *


class HWPProbeEvent:
    def __init__(self, prefetcher, obj, *listOfNames):
        self.obj = obj
        self.prefetcher = prefetcher
        self.names = listOfNames

    def register(self):
        if self.obj:
            for name in self.names:
                self.prefetcher.getCCObject().addEventProbe(
                    self.obj.getCCObject(), name
                )


class BasePrefetcher(ClockedObject):
    type = "BasePrefetcher"
    abstract = True
    cxx_class = "gem5::prefetch::Base"
    cxx_header = "mem/cache/prefetch/base.hh"
    cxx_exports = [PyBindMethod("addEventProbe"), PyBindMethod("addMMU")]
    sys = Param.System(Parent.any, "System this prefetcher belongs to")

    # Get the block size from the parent (system)
    block_size = Param.Int(Parent.cache_line_size, "Block size in bytes")

    on_miss = Param.Bool(False, "Only notify prefetcher on misses")
    on_read = Param.Bool(True, "Notify prefetcher on reads")
    on_write = Param.Bool(True, "Notify prefetcher on writes")
    on_data = Param.Bool(True, "Notify prefetcher on data accesses")
    on_inst = Param.Bool(True, "Notify prefetcher on instruction accesses")
    prefetch_on_access = Param.Bool(
        False,
        "Notify the hardware prefetcher on every access (not just misses)",
    )
    prefetch_on_pf_hit = Param.Bool(
        True,
        "Notify the hardware prefetcher on hit on prefetched lines",
    )
    use_virtual_addresses = Param.Bool(
        False, "Use virtual addresses for prefetching"
    )
    page_bytes = Param.MemorySize(
        "4KiB", "Size of pages for virtual addresses"
    )

    def __init__(self, **kwargs):
        super().__init__(**kwargs)
        self._events = []
        self._mmus = []

    def addEvent(self, newObject):
        self._events.append(newObject)

    # Override the normal SimObject::regProbeListeners method and
    # register deferred event handlers.
    def regProbeListeners(self):
        for mmu in self._mmus:
            self.getCCObject().addMMU(mmu.getCCObject())
        for event in self._events:
            event.register()
        self.getCCObject().regProbeListeners()

    def listenFromProbe(self, simObj, *probeNames):
        if not isinstance(simObj, SimObject):
            raise TypeError("argument must be of SimObject type")
        if len(probeNames) <= 0:
            raise TypeError("probeNames must have at least one element")
        self.addEvent(HWPProbeEvent(self, simObj, *probeNames))

    def registerMMU(self, simObj):
        if not isinstance(simObj, SimObject):
            raise TypeError("argument must be a SimObject type")
        self._mmus.append(simObj)


class MultiPrefetcher(BasePrefetcher):
    type = "MultiPrefetcher"
    cxx_class = "gem5::prefetch::Multi"
    cxx_header = "mem/cache/prefetch/multi.hh"

    prefetchers = VectorParam.BasePrefetcher([], "Array of prefetchers")


class IPOPMultiPrefetcher(MultiPrefetcher):
    type = "IPOPMultiPrefetcher"
    cxx_class = "gem5::prefetch::IPOPMulti"
    cxx_header = "mem/cache/prefetch/ipop_multi.hh"
    record_phase_pe_ipc_csv = Param.Bool(
        False,
        "Record each phase's PE values together with the next phase's IPC",
    )
    phase_pe_ipc_csv_path = Param.String(
        "",
        "CSV output path for phase PE and next-phase IPC logging",
    )
    phase_length = Param.Unsigned(1024, "Demand accesses per I-POP phase")
    pfht_entries = Param.Unsigned(512, "Number of PfHT entries")
    poht_entries = Param.Unsigned(512, "Number of PoHT entries")
    table_tag_bits = Param.Unsigned(6, "Tag bits stored in PfHT/PoHT")
    ipop_on_levels = Param.Unsigned(5, "Number of ON aggressiveness levels")
    ipop_off_levels = Param.Unsigned(3, "Number of OFF cooldown levels")
    ideal_dram_latency = Param.Cycles(
        100, "Ideal DRAM access latency used to derive I-POP thresholds"
    )
    phase_on_miss = Param.Bool(
        False,
        "Advance I-POP phases on completed demand misses instead of all demand accesses",
    )
    warmup_phases = Param.Unsigned(
        0,
        "Completed I-POP phases during which ON-to-OFF transitions are suppressed",
    )
    t_noc = Param.Cycles(0, "I-POP NoC contention penalty")
    t_bus = Param.Cycles(1, "I-POP DRAM bus contention penalty")
    t_bank = Param.Cycles(1, "I-POP DRAM bank contention penalty")
    channel_shift = Param.Unsigned(
        0, "Bit position of the least-significant I-POP channel index bit"
    )
    channel_bits = Param.Unsigned(
        0, "Number of I-POP channel index bits; 0 models a single channel"
    )
    bank_shift = Param.Unsigned(
        10,
        "Bit position of the least-significant I-POP bank index bit",
    )
    bank_bits = Param.Unsigned(5, "Number of I-POP bank index bits")


class BanditPrefetcher(MultiPrefetcher):
    """Micro-Armed Bandit (DUCB) prefetcher manager.

    Each arm is a bitmask over the `prefetchers` list; bit i = 1 enables
    sub-prefetcher i for that arm. The agent uses the Discounted UCB
    algorithm from Gerogiannis & Torrellas, MICRO'23.
    """

    type = "BanditPrefetcher"
    cxx_class = "gem5::prefetch::Bandit"
    cxx_header = "mem/cache/prefetch/bandit.hh"

    arm_masks = VectorParam.UInt64(
        [], "Per-arm bitmask over the sub-prefetcher list"
    )
    gamma = Param.Float(0.999, "DUCB discount factor in (0, 1]")
    c = Param.Float(0.04, "Exploration constant")
    bandit_step = Param.UInt64(
        1000, "Main-loop bandit step duration (L2 demand accesses)"
    )
    bandit_step_rr = Param.UInt64(
        1000, "Initial round-robin bandit step duration (L2 demand accesses)"
    )
    cpu = Param.BaseCPU(
        Parent.any, "CPU used to read committed instruction counts"
    )


class QueuedPrefetcher(BasePrefetcher):
    type = "QueuedPrefetcher"
    abstract = True
    cxx_class = "gem5::prefetch::Queued"
    cxx_header = "mem/cache/prefetch/queued.hh"
    latency = Param.Int(1, "Latency for generated prefetches")
    queue_size = Param.Int(32, "Maximum number of queued prefetches")
    max_prefetch_requests_with_pending_translation = Param.Int(
        32,
        "Maximum number of queued prefetches that have a missing translation",
    )
    queue_squash = Param.Bool(True, "Squash queued prefetch on demand access")
    queue_filter = Param.Bool(True, "Don't queue redundant prefetches")
    cache_snoop = Param.Bool(
        False, "Snoop cache to eliminate redundant request"
    )

    tag_prefetch = Param.Bool(
        True, "Tag prefetch with PC of generating access"
    )

    # The throttle_control_percentage controls how many of the candidate
    # addresses generated by the prefetcher will be finally turned into
    # prefetch requests
    # - If set to 100, all candidates can be discarded (one request
    #   will always be allowed to be generated)
    # - Setting it to 0 will disable the throttle control, so requests are
    #   created for all candidates
    # - If set to 60, 40% of candidates will generate a request, and the
    #   remaining 60% will be generated depending on the current accuracy
    throttle_control_percentage = Param.Percent(
        0,
        "Percentage of requests \
        that can be throttled depending on the accuracy of the prefetcher.",
    )


class SandboxMultiPrefetchers(QueuedPrefetcher):
    type = "SandboxMultiPrefetchers"
    cxx_class = "gem5::prefetch::SandboxMulti"
    cxx_header = "mem/cache/prefetch/sandbox_multi.hh"

    prefetchers = VectorParam.BasePrefetcher(
        [], "Queued child prefetchers managed by the sandbox policy"
    )
    sandbox_entries = Param.Unsigned(
        256, "Maximum number of shadow candidates retained in the sandbox"
    )
    evaluation_window = Param.Unsigned(
        256, "Number of accesses used to evaluate one child prefetcher"
    )
    score_threshold_pct = Param.Percent(
        25, "Score threshold as a percent of the evaluation window"
    )
    bandwidth_requests_per_access = Param.Float(
        2.0,
        "Target aggregate memory requests per observed access used to "
        "derive the dynamic prefetch budget",
    )
    min_prefetches_per_access = Param.Unsigned(
        0, "Minimum dynamic prefetch budget per observed access"
    )
    max_prefetches_per_access = Param.Unsigned(
        8, "Global cap on real prefetches issued per observed access"
    )
    max_prefetches_per_child = Param.Unsigned(
        3, "Maximum real prefetches a single active child may contribute"
    )
    max_active_prefetchers = Param.Unsigned(
        4, "Maximum number of active children considered on one access"
    )

    def __init__(self, **kwargs):
        super().__init__(**kwargs)
        if len(self.prefetchers) == 0:
            raise ValueError("prefetchers must not be empty")
        for pf in self.prefetchers:
            if not isinstance(pf, QueuedPrefetcher):
                raise ValueError(
                    "prefetchers must inherit from QueuedPrefetcher"
                )
        for knob in (
            "sandbox_entries",
            "evaluation_window",
            "max_prefetches_per_child",
            "max_active_prefetchers",
        ):
            if int(getattr(self, knob)) <= 0:
                raise ValueError(f"{knob} must be greater than zero")
        if float(self.bandwidth_requests_per_access) <= 0:
            raise ValueError(
                "bandwidth_requests_per_access must be greater than zero"
            )
        if int(self.max_prefetches_per_access) <= 0:
            raise ValueError(
                "max_prefetches_per_access must be greater than zero"
            )
        if int(self.min_prefetches_per_access) > int(
            self.max_prefetches_per_access
        ):
            raise ValueError(
                "min_prefetches_per_access must not exceed "
                "max_prefetches_per_access"
            )


class ReSemblePrefetcher(QueuedPrefetcher):
    type = "ReSemblePrefetcher"
    cxx_class = "gem5::prefetch::ReSemble"
    cxx_header = "mem/cache/prefetch/resemble.hh"

    prefetchers = VectorParam.QueuedPrefetcher(
        [], "Child prefetchers managed by ReSemble"
    )
    prediction_types = VectorParam.String(
        [], "Prediction type label for each child prefetcher"
    )
    hidden_dim = Param.Unsigned(32, "Hidden layer width of the controller")
    hash_bits = Param.Unsigned(12, "Feature hashing width in bits")
    alpha = Param.Float(0.01, "Learning rate of the controller")
    gamma = Param.Float(0.90, "Discount factor of the controller")
    epsilon_start = Param.Float(0.0, "Initial exploration rate")
    epsilon_end = Param.Float(0.0, "Final exploration rate")
    epsilon_decay = Param.Float(1.0, "Multiplicative epsilon decay")
    reward_window = Param.Unsigned(64, "Reward accounting window size")
    replay_capacity = Param.Unsigned(128, "Replay buffer capacity")
    batch_size = Param.Unsigned(16, "Mini-batch size for controller updates")
    policy_update_interval = Param.Unsigned(
        1, "Number of accesses between online policy updates"
    )
    target_update_interval = Param.Unsigned(
        8, "Number of accesses between target network refreshes"
    )
    seed = Param.Unsigned(1, "Deterministic seed for controller RNG")

    _positive_controller_knobs = (
        "hidden_dim",
        "hash_bits",
        "reward_window",
        "replay_capacity",
        "batch_size",
        "policy_update_interval",
        "target_update_interval",
    )
    _supported_prediction_types = frozenset(("spatial", "temporal"))

    def __init__(self, **kwargs):
        super().__init__(**kwargs)

        if len(self.prefetchers) == 0:
            raise ValueError("prefetchers must not be empty")

        if len(self.prefetchers) != len(self.prediction_types):
            raise ValueError(
                "prefetchers and prediction_types must have the same length"
            )

        for prediction_type in self.prediction_types:
            if prediction_type not in self._supported_prediction_types:
                raise ValueError(
                    f"unsupported prediction_type '{prediction_type}'"
                )

        for knob in self._positive_controller_knobs:
            if int(getattr(self, knob)) <= 0:
                raise ValueError(f"{knob} must be positive")

        if float(self.alpha) <= 0.0:
            raise ValueError("alpha must be positive")
        if not 0.0 <= float(self.gamma) <= 1.0:
            raise ValueError("gamma must be in [0, 1]")
        if not 0.0 <= float(self.epsilon_start) <= 1.0:
            raise ValueError("epsilon_start must be in [0, 1]")
        if not 0.0 <= float(self.epsilon_end) <= 1.0:
            raise ValueError("epsilon_end must be in [0, 1]")
        if float(self.epsilon_end) > float(self.epsilon_start):
            raise ValueError("epsilon_end must not exceed epsilon_start")
        if not 0.0 < float(self.epsilon_decay) <= 1.0:
            raise ValueError("epsilon_decay must be in (0, 1]")


class StridePrefetcherHashedSetAssociative(TaggedSetAssociative):
    type = "StridePrefetcherHashedSetAssociative"
    cxx_class = "gem5::prefetch::StridePrefetcherHashedSetAssociative"
    cxx_header = "mem/cache/prefetch/stride.hh"


class StridePrefetcher(QueuedPrefetcher):
    type = "StridePrefetcher"
    cxx_class = "gem5::prefetch::Stride"
    cxx_header = "mem/cache/prefetch/stride.hh"

    # Do not consult stride prefetcher on instruction accesses
    on_inst = False

    confidence_counter_bits = Param.Unsigned(
        3, "Number of bits of the confidence counter"
    )
    initial_confidence = Param.Unsigned(
        4, "Starting confidence of new entries"
    )
    confidence_threshold = Param.Percent(
        50, "Prefetch generation confidence threshold"
    )

    use_requestor_id = Param.Bool(True, "Use requestor id based history")

    use_cache_line_address = Param.Bool(
        True,
        "If this parameter is set to True, then the prefetcher will "
        "operate on cache line addresses, else it would operate on word "
        "addresses",
    )

    degree = Param.Int(4, "Number of prefetches to generate")
    distance = Param.Unsigned(
        0,
        "How far ahead of the demand stream to start prefetching. "
        "Skip this number of strides ahead of the first identified prefetch, "
        "then generate `degree` prefetches at `stride` intervals. "
        "A value of zero indicates no skip.",
    )

    table_assoc = Param.Int(4, "Associativity of the PC table")
    table_entries = Param.MemorySize("64", "Number of entries of the PC table")
    table_indexing_policy = Param.TaggedIndexingPolicy(
        StridePrefetcherHashedSetAssociative(
            entry_size=1, assoc=Parent.table_assoc, size=Parent.table_entries
        ),
        "Indexing policy of the PC table",
    )
    table_replacement_policy = Param.BaseReplacementPolicy(
        RandomRP(), "Replacement policy of the PC table"
    )


class AMDContiguousStreamPrefetcher(QueuedPrefetcher):
    type = "AMDContiguousStreamPrefetcher"
    cxx_class = "gem5::prefetch::AMDContiguousStreamPrefetcher"
    cxx_header = "mem/cache/prefetch/amd_contiguous_stream.hh"

    stream_entries = Param.Unsigned(16, "Active contiguous stream entries")
    last_access_entries = Param.Unsigned(
        16, "Recent accesses used to create new streams"
    )
    degree = Param.Unsigned(4, "Maximum prefetches per stream update")
    use_requestor_id = Param.Bool(False, "Include requestor ID in matching")

    prefetch_on_access = True
    prefetch_on_pf_hit = False
    on_inst = False


class TaggedPrefetcher(QueuedPrefetcher):
    type = "TaggedPrefetcher"
    cxx_class = "gem5::prefetch::Tagged"
    cxx_header = "mem/cache/prefetch/tagged.hh"

    degree = Param.Int(2, "Number of prefetches to generate")


class IndirectMemoryPrefetcher(QueuedPrefetcher):
    type = "IndirectMemoryPrefetcher"
    cxx_class = "gem5::prefetch::IndirectMemory"
    cxx_header = "mem/cache/prefetch/indirect_memory.hh"
    pt_table_entries = Param.MemorySize(
        "16", "Number of entries of the Prefetch Table"
    )
    pt_table_assoc = Param.Unsigned(16, "Associativity of the Prefetch Table")
    pt_table_indexing_policy = Param.TaggedIndexingPolicy(
        TaggedSetAssociative(
            entry_size=1,
            assoc=Parent.pt_table_assoc,
            size=Parent.pt_table_entries,
        ),
        "Indexing policy of the pattern table",
    )
    pt_table_replacement_policy = Param.BaseReplacementPolicy(
        LRURP(), "Replacement policy of the pattern table"
    )
    max_prefetch_distance = Param.Unsigned(16, "Maximum prefetch distance")
    num_indirect_counter_bits = Param.Unsigned(
        3, "Number of bits of the indirect counter"
    )
    ipd_table_entries = Param.MemorySize(
        "4", "Number of entries of the Indirect Pattern Detector"
    )
    ipd_table_assoc = Param.Unsigned(
        4, "Associativity of the Indirect Pattern Detector"
    )
    ipd_table_indexing_policy = Param.TaggedIndexingPolicy(
        TaggedSetAssociative(
            entry_size=1,
            assoc=Parent.ipd_table_assoc,
            size=Parent.ipd_table_entries,
        ),
        "Indexing policy of the Indirect Pattern Detector",
    )
    ipd_table_replacement_policy = Param.BaseReplacementPolicy(
        LRURP(), "Replacement policy of the Indirect Pattern Detector"
    )
    shift_values = VectorParam.Int([2, 3, 4, -3], "Shift values to evaluate")
    addr_array_len = Param.Unsigned(4, "Number of misses tracked")
    prefetch_threshold = Param.Unsigned(
        2, "Counter threshold to start the indirect prefetching"
    )
    stream_counter_threshold = Param.Unsigned(
        4, "Counter threshold to enable the stream prefetcher"
    )
    streaming_distance = Param.Unsigned(
        4, "Number of prefetches to generate when using the stream prefetcher"
    )


class ARMOffsetBasedPointerPrefetcher(QueuedPrefetcher):
    type = "ARMOffsetBasedPointerPrefetcher"
    cxx_class = "gem5::prefetch::ARMOffsetBasedPointerPrefetcher"
    cxx_header = "mem/cache/prefetch/arm_offset_based_pointer.hh"

    on_inst = False
    prefetch_on_access = True

    history_entries = Param.Unsigned(
        64, "Number of trigger access PCs retained in the history buffer"
    )
    pointer_cache_entries = Param.Unsigned(
        64, "Number of recent detected pointers retained in the pointer cache"
    )
    structure_entries = Param.Unsigned(
        64, "Number of learned data structure relationships"
    )
    pending_entries = Param.Unsigned(
        32, "Number of pending pointer-line prefetches"
    )
    spatial_entries = Param.Unsigned(
        8, "Number of SMS-style offsets retained per trigger PC"
    )
    recent_pointer_search_entries = Param.Unsigned(
        16, "Recent pointer cache entries searched while learning"
    )
    max_element_bytes = Param.MemorySize(
        "512B", "Maximum trigger-to-trigger distance considered structural"
    )
    max_pointer_offset_bytes = Param.MemorySize(
        "256B", "Maximum pointer-location offset considered structural"
    )
    max_pointer_target_offset_bytes = Param.MemorySize(
        "256B", "Maximum pointer-target to trigger offset"
    )
    min_pointer_address = Param.Addr(
        4096, "Ignore candidate pointer values below this address"
    )
    pointer_bytes = Param.Unsigned(
        8, "Pointer detector width in bytes; use 4 for 32-bit targets"
    )
    pointer_msw_match_bits = Param.Unsigned(
        16, "Most-significant address bits that must match pointer context"
    )
    pointer_align_bits = Param.Unsigned(
        3, "Required low zero bits for pointer candidates"
    )
    confidence_bits = Param.Unsigned(
        3, "Bits in learned-relationship confidence counters"
    )
    min_confidence = Param.Unsigned(
        2, "Minimum relationship confidence before issuing prefetches"
    )
    degree = Param.Unsigned(
        2, "Number of table-structure data prefetches generated per access"
    )
    lookahead = Param.Unsigned(
        2, "Number of dependent pointer dereferences to look ahead"
    )
    scan_cacheline_on_fill = Param.Bool(
        True, "Scan filled cache lines for pointer candidates"
    )
    enable_table_detector = Param.Bool(
        True, "Learn constant trigger-address displacement structures"
    )
    enable_linked_list_detector = Param.Bool(
        True, "Learn pointer-inside-current-element linked-list structures"
    )
    enable_pointer_table_detector = Param.Bool(
        True, "Learn arrays of pointers to data elements"
    )


class SignaturePathPrefetcher(QueuedPrefetcher):
    type = "SignaturePathPrefetcher"
    cxx_class = "gem5::prefetch::SignaturePath"
    cxx_header = "mem/cache/prefetch/signature_path.hh"

    signature_shift = Param.UInt8(
        3, "Number of bits to shift when calculating a new signature"
    )
    signature_bits = Param.UInt16(12, "Size of the signature, in bits")
    signature_table_entries = Param.MemorySize(
        "1024", "Number of entries of the signature table"
    )
    signature_table_assoc = Param.Unsigned(
        2, "Associativity of the signature table"
    )
    signature_table_indexing_policy = Param.TaggedIndexingPolicy(
        TaggedSetAssociative(
            entry_size=1,
            assoc=Parent.signature_table_assoc,
            size=Parent.signature_table_entries,
        ),
        "Indexing policy of the signature table",
    )
    signature_table_replacement_policy = Param.BaseReplacementPolicy(
        LRURP(), "Replacement policy of the signature table"
    )

    num_counter_bits = Param.UInt8(
        3, "Number of bits of the saturating counters"
    )
    pattern_table_entries = Param.MemorySize(
        "4096", "Number of entries of the pattern table"
    )
    pattern_table_assoc = Param.Unsigned(
        1, "Associativity of the pattern table"
    )
    strides_per_pattern_entry = Param.Unsigned(
        4, "Number of strides stored in each pattern entry"
    )
    pattern_table_indexing_policy = Param.TaggedIndexingPolicy(
        TaggedSetAssociative(
            entry_size=1,
            assoc=Parent.pattern_table_assoc,
            size=Parent.pattern_table_entries,
        ),
        "Indexing policy of the pattern table",
    )
    pattern_table_replacement_policy = Param.BaseReplacementPolicy(
        LRURP(), "Replacement policy of the pattern table"
    )

    prefetch_confidence_threshold = Param.Float(
        0.5, "Minimum confidence to issue prefetches"
    )
    lookahead_confidence_threshold = Param.Float(
        0.75, "Minimum confidence to continue exploring lookahead entries"
    )


class SignaturePathPrefetcherV2(SignaturePathPrefetcher):
    type = "SignaturePathPrefetcherV2"
    cxx_class = "gem5::prefetch::SignaturePathV2"
    cxx_header = "mem/cache/prefetch/signature_path_v2.hh"

    signature_table_entries = "256"
    signature_table_assoc = 1
    pattern_table_entries = "512"
    pattern_table_assoc = 1
    num_counter_bits = 4
    prefetch_confidence_threshold = 0.25
    lookahead_confidence_threshold = 0.25

    global_history_register_entries = Param.MemorySize(
        "8", "Number of entries of global history register"
    )
    global_history_register_indexing_policy = Param.TaggedIndexingPolicy(
        TaggedSetAssociative(
            entry_size=1,
            assoc=Parent.global_history_register_entries,
            size=Parent.global_history_register_entries,
        ),
        "Indexing policy of the global history register",
    )
    global_history_register_replacement_policy = Param.BaseReplacementPolicy(
        LRURP(), "Replacement policy of the global history register"
    )


class AccessMapPatternMatching(ClockedObject):
    type = "AccessMapPatternMatching"
    cxx_class = "gem5::prefetch::AccessMapPatternMatching"
    cxx_header = "mem/cache/prefetch/access_map_pattern_matching.hh"

    block_size = Param.Unsigned(
        Parent.block_size,
        "Cacheline size used by the prefetcher using this object",
    )

    limit_stride = Param.Unsigned(
        0, "Limit the strides checked up to -X/X, if 0, disable the limit"
    )
    start_degree = Param.Unsigned(
        4, "Initial degree (Maximum number of prefetches generated"
    )
    hot_zone_size = Param.MemorySize("2KiB", "Memory covered by a hot zone")
    access_map_table_entries = Param.MemorySize(
        "256", "Number of entries in the access map table"
    )
    access_map_table_assoc = Param.Unsigned(
        8, "Associativity of the access map table"
    )
    access_map_table_indexing_policy = Param.TaggedIndexingPolicy(
        TaggedSetAssociative(
            entry_size=1,
            assoc=Parent.access_map_table_assoc,
            size=Parent.access_map_table_entries,
        ),
        "Indexing policy of the access map table",
    )
    access_map_table_replacement_policy = Param.BaseReplacementPolicy(
        LRURP(), "Replacement policy of the access map table"
    )
    high_coverage_threshold = Param.Float(
        0.25, "A prefetch coverage factor bigger than this is considered high"
    )
    low_coverage_threshold = Param.Float(
        0.125, "A prefetch coverage factor smaller than this is considered low"
    )
    high_accuracy_threshold = Param.Float(
        0.5, "A prefetch accuracy factor bigger than this is considered high"
    )
    low_accuracy_threshold = Param.Float(
        0.25, "A prefetch accuracy factor smaller than this is considered low"
    )
    high_cache_hit_threshold = Param.Float(
        0.875, "A cache hit ratio bigger than this is considered high"
    )
    low_cache_hit_threshold = Param.Float(
        0.75, "A cache hit ratio smaller than this is considered low"
    )
    epoch_cycles = Param.Cycles(256000, "Cycles in an epoch period")
    offchip_memory_latency = Param.Latency(
        "30ns", "Memory latency used to compute the required memory bandwidth"
    )


class AMPMPrefetcher(QueuedPrefetcher):
    type = "AMPMPrefetcher"
    cxx_class = "gem5::prefetch::AMPM"
    cxx_header = "mem/cache/prefetch/access_map_pattern_matching.hh"
    ampm = Param.AccessMapPatternMatching(
        AccessMapPatternMatching(), "Access Map Pattern Matching object"
    )


class AppleAMPMPrefetcher(QueuedPrefetcher):
    type = "AppleAMPMPrefetcher"
    cxx_class = "gem5::prefetch::AppleAMPM"
    cxx_header = "mem/cache/prefetch/apple_ampm.hh"

    on_inst = False
    prefetch_on_access = True
    prefetch_on_pf_hit = True

    limit_stride = Param.Unsigned(
        0, "Limit the strides checked up to -X/X; zero disables the limit"
    )
    degree = Param.Unsigned(4, "Maximum prefetches generated per access")
    hot_zone_size = Param.MemorySize("2KiB", "Memory covered by a hot zone")

    access_map_table_entries = Param.MemorySize(
        "256", "Number of entries in the access map table"
    )
    access_map_table_assoc = Param.Unsigned(
        8, "Associativity of the access map table"
    )
    access_map_table_indexing_policy = Param.TaggedIndexingPolicy(
        TaggedSetAssociative(
            entry_size=1,
            assoc=Parent.access_map_table_assoc,
            size=Parent.access_map_table_entries,
        ),
        "Indexing policy of the access map table",
    )
    access_map_table_replacement_policy = Param.BaseReplacementPolicy(
        LRURP(), "Replacement policy of the access map table"
    )

    initial_quality_factor = Param.Unsigned(
        75, "Initial per-access-map quality factor tokens"
    )
    max_quality_factor = Param.Unsigned(
        100, "Maximum per-access-map quality factor tokens"
    )
    prefetch_token_cost = Param.Unsigned(
        8, "Quality factor tokens consumed by a non-store-only prefetch"
    )
    store_only_prefetch_token_cost = Param.Unsigned(
        10, "Quality factor tokens consumed by a store-only prefetch"
    )
    successful_prefetch_tokens = Param.Unsigned(
        12, "Quality factor tokens restored by a successful prefetch"
    )
    cache_hit_penalty_tokens = Param.Unsigned(
        4, "Quality factor tokens removed when a generated prefetch hits cache"
    )
    pointer_prefetch_tokens = Param.Unsigned(
        12, "Quality factor tokens restored when pointer activity is active"
    )
    quality_factor_bypass_accesses = Param.Unsigned(
        0,
        "Bypass quality factor after this many accessed lines in a map; "
        "zero means never bypass",
    )

    use_pointer_value_heuristic = Param.Bool(
        True,
        "Approximate pointer-read detection by tracking loaded values that "
        "are later used as load addresses",
    )
    pointer_field_max = Param.Unsigned(15, "Maximum pointer field value")
    pointer_initial_value = Param.Unsigned(0, "Initial pointer field value")
    pointer_increment = Param.Unsigned(
        4, "Pointer field increment for detected pointer reads"
    )
    pointer_decrement = Param.Unsigned(
        1, "Pointer field decrement for load accesses without pointer signal"
    )
    pointer_threshold = Param.Unsigned(
        1, "Pointer field threshold that marks pointer activity active"
    )
    pointer_tracking_entries = Param.Unsigned(
        64, "Loaded pointer-like values retained for future load matching"
    )
    pointer_tracking_window = Param.Unsigned(
        256,
        "Maximum later accesses before a retained pointer-like value ages out",
    )
    pointer_min_addr = Param.Addr(
        4096, "Minimum loaded value considered as a possible pointer"
    )
    pointer_value_distance = Param.MemorySize(
        "0B",
        "Optional maximum distance between the load address and loaded value; "
        "zero disables the locality filter",
    )


class DeltaCorrelatingPredictionTables(SimObject):
    type = "DeltaCorrelatingPredictionTables"
    cxx_class = "gem5::prefetch::DeltaCorrelatingPredictionTables"
    cxx_header = "mem/cache/prefetch/delta_correlating_prediction_tables.hh"
    deltas_per_entry = Param.Unsigned(
        20, "Number of deltas stored in each table entry"
    )
    delta_bits = Param.Unsigned(12, "Bits per delta")
    delta_mask_bits = Param.Unsigned(
        8, "Lower bits to mask when comparing deltas"
    )
    table_entries = Param.MemorySize("128", "Number of entries in the table")
    table_assoc = Param.Unsigned(128, "Associativity of the table")
    table_indexing_policy = Param.BaseIndexingPolicy(
        SetAssociative(
            entry_size=1, assoc=Parent.table_assoc, size=Parent.table_entries
        ),
        "Indexing policy of the table",
    )
    table_replacement_policy = Param.BaseReplacementPolicy(
        LRURP(), "Replacement policy of the table"
    )


class DCPTPrefetcher(QueuedPrefetcher):
    type = "DCPTPrefetcher"
    cxx_class = "gem5::prefetch::DCPT"
    cxx_header = "mem/cache/prefetch/delta_correlating_prediction_tables.hh"
    dcpt = Param.DeltaCorrelatingPredictionTables(
        DeltaCorrelatingPredictionTables(),
        "Delta Correlating Prediction Tables object",
    )


class IrregularStreamBufferPrefetcher(QueuedPrefetcher):
    type = "IrregularStreamBufferPrefetcher"
    cxx_class = "gem5::prefetch::IrregularStreamBuffer"
    cxx_header = "mem/cache/prefetch/irregular_stream_buffer.hh"

    num_counter_bits = Param.Unsigned(
        2, "Number of bits of the confidence counter"
    )
    chunk_size = Param.Unsigned(
        256, "Maximum number of addresses in a temporal stream"
    )
    degree = Param.Unsigned(4, "Number of prefetches to generate")
    training_unit_assoc = Param.Unsigned(
        128, "Associativity of the training unit"
    )
    training_unit_entries = Param.MemorySize(
        "128", "Number of entries of the training unit"
    )
    training_unit_indexing_policy = Param.TaggedIndexingPolicy(
        TaggedSetAssociative(
            entry_size=1,
            assoc=Parent.training_unit_assoc,
            size=Parent.training_unit_entries,
        ),
        "Indexing policy of the training unit",
    )
    training_unit_replacement_policy = Param.BaseReplacementPolicy(
        LRURP(), "Replacement policy of the training unit"
    )

    prefetch_candidates_per_entry = Param.Unsigned(
        16, "Number of prefetch candidates stored in a SP-AMC entry"
    )
    address_map_cache_assoc = Param.Unsigned(
        128, "Associativity of the PS/SP AMCs"
    )
    address_map_cache_entries = Param.MemorySize(
        "128", "Number of entries of the PS/SP AMCs"
    )
    ps_address_map_cache_indexing_policy = Param.TaggedIndexingPolicy(
        TaggedSetAssociative(
            entry_size=1,
            assoc=Parent.address_map_cache_assoc,
            size=Parent.address_map_cache_entries,
        ),
        "Indexing policy of the Physical-to-Structural Address Map Cache",
    )
    ps_address_map_cache_replacement_policy = Param.BaseReplacementPolicy(
        LRURP(),
        "Replacement policy of the Physical-to-Structural Address Map Cache",
    )
    sp_address_map_cache_indexing_policy = Param.TaggedIndexingPolicy(
        TaggedSetAssociative(
            entry_size=1,
            assoc=Parent.address_map_cache_assoc,
            size=Parent.address_map_cache_entries,
        ),
        "Indexing policy of the Structural-to-Physical Address Mao Cache",
    )
    sp_address_map_cache_replacement_policy = Param.BaseReplacementPolicy(
        LRURP(),
        "Replacement policy of the Structural-to-Physical Address Map Cache",
    )


class SlimAccessMapPatternMatching(AccessMapPatternMatching):
    start_degree = 2
    limit_stride = 4


class SlimDeltaCorrelatingPredictionTables(DeltaCorrelatingPredictionTables):
    table_entries = "256"
    table_assoc = 256
    deltas_per_entry = 9


class SlimAMPMPrefetcher(QueuedPrefetcher):
    type = "SlimAMPMPrefetcher"
    cxx_class = "gem5::prefetch::SlimAMPM"
    cxx_header = "mem/cache/prefetch/slim_ampm.hh"

    ampm = Param.AccessMapPatternMatching(
        SlimAccessMapPatternMatching(), "Access Map Pattern Matching object"
    )
    dcpt = Param.DeltaCorrelatingPredictionTables(
        SlimDeltaCorrelatingPredictionTables(),
        "Delta Correlating Prediction Tables object",
    )


class BertiPrefetcher(QueuedPrefetcher):
    type = "BertiPrefetcher"
    cxx_class = "gem5::prefetch::BertiPrefetcher"
    cxx_header = "mem/cache/prefetch/berti.hh"

    use_virtual_addresses = True
    prefetch_on_pf_hit = True
    on_read = True
    on_write = False
    on_data = True
    on_inst = False

    addrlist_size = Param.Int(6, "The size of address list")

    deltalist_size = Param.Int(4, "The size of delta list")

    max_deltafound = Param.Int(4, "The maximum number of delta can be found")

    aggressive_pf = Param.Bool(False, "Issue pf reqs as many as possible.")
    history_table_entries = Param.MemorySize(
        "64", "Number of history table entries."
    )
    history_table_assoc = Param.Int(4, "Associativity of the history table.")
    history_table_indexing_policy = Param.BaseIndexingPolicy(
        SetAssociative(
            entry_size=1,
            assoc=Parent.history_table_assoc,
            size=Parent.history_table_entries,
        ),
        "Indexing policy of history table.",
    )
    history_table_replacement_policy = Param.BaseReplacementPolicy(
        LRURP(), "Replacement policy of history table"
    )
    use_byte_addr = Param.Bool(True, "Use byte address")
    trigger_pht = Param.Bool(True, "Use Berti's prediction to trigger PHT")
    dump_top_deltas = Param.Bool(True, "Dump top deltas on exit")

    shared_pf_filter = Param.Bool(
        True, "Use shared prefetch Filter with other prefetcher."
    )
    pf_filter_size = Param.Int(
        8, "The size of prefetch filter (valid when shared_pf_filter is False)"
    )


class BOPPrefetcher(QueuedPrefetcher):
    type = "BOPPrefetcher"
    cxx_class = "gem5::prefetch::BOP"
    cxx_header = "mem/cache/prefetch/bop.hh"
    score_max = Param.Unsigned(31, "Max. score to update the best offset")
    round_max = Param.Unsigned(100, "Max. round to update the best offset")
    bad_score = Param.Unsigned(10, "Score at which the HWP is disabled")
    rr_size = Param.Unsigned(64, "Number of entries of each RR bank")
    tag_bits = Param.Unsigned(12, "Bits used to store the tag")
    offset_list_size = Param.Unsigned(
        46, "Number of entries in the offsets list"
    )
    negative_offsets_enable = Param.Bool(
        True,
        "Initialize the offsets list also with negative values \
                (i.e. the table will have half of the entries with positive \
                offsets and the other half with negative ones)",
    )
    delay_queue_enable = Param.Bool(True, "Enable the delay queue")
    delay_queue_size = Param.Unsigned(
        15, "Number of entries in the delay queue"
    )
    delay_queue_cycles = Param.Cycles(
        60,
        "Cycles to delay a write in the left RR table from the delay \
                queue",
    )

    # BOP is a degree one prefetcher
    degree = Param.Int(1, "Number of prefetches to generate")

    queue_squash = True
    queue_filter = True
    cache_snoop = True
    prefetch_on_pf_hit = True
    on_miss = True
    on_inst = False


class SmsPrefetcher(QueuedPrefetcher):
    # Paper: https://web.eecs.umich.edu/~twenisch/papers/isca06.pdf
    type = "SmsPrefetcher"
    cxx_class = "gem5::prefetch::Sms"
    cxx_header = "mem/cache/prefetch/sms.hh"
    ft_size = Param.Unsigned(64, "Size of Filter and Active generation table")
    pht_size = Param.Unsigned(16384, "Size of pattern history table")
    region_size = Param.Unsigned(4096, "Spatial region size")

    queue_squash = True
    queue_filter = True
    cache_snoop = True
    prefetch_on_access = True
    on_inst = False


class BingoPrefetcher(QueuedPrefetcher):
    # Paper: Bakhshalipour et al., HPCA 2019
    # https://www.cs.ucsb.edu/~chong/290N-W18/Bingo.pdf
    type = "BingoPrefetcher"
    cxx_class = "gem5::prefetch::Bingo"
    cxx_header = "mem/cache/prefetch/bingo.hh"

    region_size = Param.Unsigned(2048, "Spatial region (page) size in bytes")
    pattern_len = Param.Unsigned(
        32, "Blocks per region (must equal region_size / blkSize)"
    )
    ft_size = Param.Unsigned(64, "FilterTable entries (fully-assoc, LRU)")
    at_size = Param.Unsigned(
        128, "AccumulationTable entries (fully-assoc, LRU)"
    )
    pht_size = Param.Unsigned(
        16384,
        "Total PHT entries (must be a multiple of pht_ways, "
        "pht_size/pht_ways must be power of two)",
    )
    pht_ways = Param.Unsigned(16, "PHT associativity")
    pc_width = Param.Unsigned(16, "PC bits used in tag/key")
    min_addr_width = Param.Unsigned(
        5, "Offset width in bits (log2(pattern_len))"
    )
    max_addr_width = Param.Unsigned(
        16, "Address bits used in max (PC+Address) tag"
    )
    thresh = Param.Float(
        0.20, "Voting threshold for PC+Offset min-match candidates"
    )
    rotate_pattern = Param.Bool(
        True, "Rotate pattern by -offset on insert / +offset on find"
    )

    prefetch_on_access = True
    prefetch_on_pf_hit = False
    on_inst = False


class AMDRegionTypePrefetcher(QueuedPrefetcher):
    type = "AMDRegionTypePrefetcher"
    cxx_class = "gem5::prefetch::AMDRegionTypePrefetcher"
    cxx_header = "mem/cache/prefetch/amd_region_type.hh"

    region_size = Param.Unsigned(2048, "Memory region size in bytes")
    observation_entries = Param.Unsigned(
        64, "Entries in the pattern observation table"
    )
    region_type_entries = Param.Unsigned(
        512, "Entries in the region-address to region-type table"
    )
    recorded_pattern_entries = Param.Unsigned(
        1024, "Entries in the region-type to recorded-pattern table"
    )
    observation_window = Param.Unsigned(
        256, "Access-count window before completing an active observation"
    )
    observation_timeout = Param.Unsigned(
        1024, "Idle access-count timeout before completing an observation"
    )
    duplicate_prefetch_window = Param.Unsigned(
        32, "Access-count window suppressing duplicate pattern replays"
    )
    recorded_pattern_assoc = Param.Unsigned(
        16, "Logical associativity for recorded-pattern way metadata"
    )
    region_type_candidates = Param.Unsigned(
        2, "Candidate recorded patterns retained per region-type entry"
    )
    use_requestor_id = Param.Bool(
        False, "Include RequestorID in region observation and mapping keys"
    )
    confidence_counter_bits = Param.Unsigned(
        3, "Bits in recorded-pattern and region-type confidence counters"
    )
    initial_confidence = Param.Unsigned(
        4, "Initial confidence for newly learned region-type mappings"
    )
    confidence_threshold = Param.Unsigned(
        2, "Minimum confidence required before replaying a recorded pattern"
    )
    aging_interval = Param.Unsigned(
        4096, "Access-count interval for confidence aging"
    )
    similarity_threshold = Param.Unsigned(
        2, "Maximum exclusive Hamming distance for near pattern matching"
    )
    min_pattern_bits = Param.Unsigned(
        2, "Minimum set subdivision bits required before installing a pattern"
    )
    degree = Param.Unsigned(8, "Maximum prefetches generated per trigger")
    prefetch_distance = Param.Unsigned(
        0, "Maximum byte distance from trigger; zero means full region"
    )
    merge_policy = Param.String(
        "or", "Merge policy for matching patterns: or, and, or replace"
    )
    prefetch_current = Param.Bool(
        False, "Allow replay to prefetch the triggering subdivision"
    )

    queue_squash = True
    queue_filter = True
    cache_snoop = True


class AMDRIPRegionPrefetcher(QueuedPrefetcher):
    type = "AMDRIPRegionPrefetcher"
    cxx_class = "gem5::prefetch::AMDRIPRegionPrefetcher"
    cxx_header = "mem/cache/prefetch/amd_rip_region.hh"

    line_entry_entries = Param.Unsigned(
        32, "Entries in the line entry training table"
    )
    region_history_entries = Param.Unsigned(
        512, "Entries in the RIP/Addr[5:4] region history table"
    )
    negative_lines = Param.Unsigned(
        4, "Cache lines before the home line covered by a region"
    )
    positive_lines = Param.Unsigned(
        6, "Cache lines after the home line covered by a region"
    )
    rip_bits = Param.Unsigned(20, "Low RIP bits used by the predictor")
    address_offset_shift = Param.Unsigned(
        4, "First address bit in the line-alignment offset field"
    )
    address_offset_bits = Param.Unsigned(
        2, "Number of line-alignment offset bits"
    )
    counter_bits = Param.Unsigned(
        2, "Bits per region-history line-offset counter"
    )
    counter_threshold = Param.Unsigned(
        2, "Minimum counter value required to issue a prefetch"
    )
    min_pattern_bits = Param.Unsigned(
        2, "Minimum non-home lines needed to train a pseudo-random pattern"
    )
    use_requestor_id = Param.Bool(False, "Include requestor ID in matching")
    degree = Param.Unsigned(10, "Maximum prefetches to generate per miss")

    prefetch_on_access = True
    prefetch_on_pf_hit = False
    on_inst = False


class AMDRegionStreamPrefetchers(BasePrefetcher):
    type = "AMDRegionStreamPrefetchers"
    cxx_class = "gem5::prefetch::AMDRegionStreamPrefetchers"
    cxx_header = "mem/cache/prefetch/amd_region_stream.hh"

    stream_prefetcher = Param.BasePrefetcher(
        AMDContiguousStreamPrefetcher(), "Stream prefetcher child"
    )
    region_prefetcher = Param.BasePrefetcher(
        AMDRIPRegionPrefetcher(), "AMD RIP region prefetcher child"
    )
    block_stream_on_region_pending = Param.Bool(
        True,
        "Block stream processing while region prefetch requests are pending",
    )

    prefetch_on_access = True
    prefetch_on_pf_hit = False
    on_inst = False


class SBOOEPrefetcher(QueuedPrefetcher):
    type = "SBOOEPrefetcher"
    cxx_class = "gem5::prefetch::SBOOE"
    cxx_header = "mem/cache/prefetch/sbooe.hh"
    latency_buffer_size = Param.Int(32, "Entries in the latency buffer")
    sequential_prefetchers = Param.Int(9, "Number of sequential prefetchers")
    sandbox_entries = Param.Int(1024, "Size of the address buffer")
    score_threshold_pct = Param.Percent(
        25,
        "Min. threshold to issue a \
        prefetch. The value is the percentage of sandbox entries to use",
    )


class STeMSPrefetcher(QueuedPrefetcher):
    type = "STeMSPrefetcher"
    cxx_class = "gem5::prefetch::STeMS"
    cxx_header = "mem/cache/prefetch/spatio_temporal_memory_streaming.hh"

    spatial_region_size = Param.MemorySize(
        "2KiB", "Memory covered by a hot zone"
    )
    active_generation_table_entries = Param.MemorySize(
        "64", "Number of entries in the active generation table"
    )
    active_generation_table_assoc = Param.Unsigned(
        64, "Associativity of the active generation table"
    )
    active_generation_table_indexing_policy = Param.TaggedIndexingPolicy(
        TaggedSetAssociative(
            entry_size=1,
            assoc=Parent.active_generation_table_assoc,
            size=Parent.active_generation_table_entries,
        ),
        "Indexing policy of the active generation table",
    )
    active_generation_table_replacement_policy = Param.BaseReplacementPolicy(
        LRURP(), "Replacement policy of the active generation table"
    )

    pattern_sequence_table_entries = Param.MemorySize(
        "16384", "Number of entries in the pattern sequence table"
    )
    pattern_sequence_table_assoc = Param.Unsigned(
        16384, "Associativity of the pattern sequence table"
    )
    pattern_sequence_table_indexing_policy = Param.TaggedIndexingPolicy(
        TaggedSetAssociative(
            entry_size=1,
            assoc=Parent.pattern_sequence_table_assoc,
            size=Parent.pattern_sequence_table_entries,
        ),
        "Indexing policy of the pattern sequence table",
    )
    pattern_sequence_table_replacement_policy = Param.BaseReplacementPolicy(
        LRURP(), "Replacement policy of the pattern sequence table"
    )

    region_miss_order_buffer_entries = Param.Unsigned(
        131072, "Number of entries of the Region Miss Order Buffer"
    )
    add_duplicate_entries_to_rmob = Param.Bool(
        True, "Add duplicate entries to RMOB"
    )
    reconstruction_entries = Param.Unsigned(
        256, "Number of reconstruction entries"
    )


class HWPProbeEventRetiredInsts(HWPProbeEvent):
    def register(self):
        if self.obj:
            for name in self.names:
                self.prefetcher.getCCObject().addEventProbeRetiredInsts(
                    self.obj.getCCObject(), name
                )


class PIFPrefetcher(QueuedPrefetcher):
    type = "PIFPrefetcher"
    cxx_class = "gem5::prefetch::PIF"
    cxx_header = "mem/cache/prefetch/pif.hh"
    cxx_exports = [PyBindMethod("addEventProbeRetiredInsts")]

    prec_spatial_region_bits = Param.Unsigned(
        2, "Number of preceding addresses in the spatial region"
    )
    succ_spatial_region_bits = Param.Unsigned(
        8, "Number of subsequent addresses in the spatial region"
    )
    compactor_entries = Param.Unsigned(2, "Entries in the temp. compactor")
    stream_address_buffer_entries = Param.Unsigned(7, "Entries in the SAB")
    history_buffer_size = Param.Unsigned(16, "Entries in the history buffer")

    index_entries = Param.MemorySize("64", "Number of entries in the index")
    index_assoc = Param.Unsigned(64, "Associativity of the index")
    index_indexing_policy = Param.TaggedIndexingPolicy(
        TaggedSetAssociative(
            entry_size=1, assoc=Parent.index_assoc, size=Parent.index_entries
        ),
        "Indexing policy of the index",
    )
    index_replacement_policy = Param.BaseReplacementPolicy(
        LRURP(), "Replacement policy of the index"
    )

    def listenFromProbeRetiredInstructions(self, simObj):
        if not isinstance(simObj, SimObject):
            raise TypeError("argument must be of SimObject type")
        self.addEvent(
            HWPProbeEventRetiredInsts(self, simObj, "RetiredInstsPC")
        )


class KairosPrefetcher(QueuedPrefetcher):
    type = "KairosPrefetcher"
    cxx_class = "gem5::prefetch::Kairos"
    cxx_header = "mem/cache/prefetch/kairos.hh"

    degree = Param.Unsigned(4, "Max chain-walk prefetches per access")
    kd_size = Param.Unsigned(32, "Detecting Unit entries")
    tu_size = Param.Unsigned(16, "Training Unit entries")
    ht_sets = Param.Unsigned(4096, "Metadata cache sets")
    ht_ways_init = Param.Unsigned(48, "Initial metadata ways per set")
    ht_ways_min = Param.Unsigned(12, "Minimum metadata ways per set")
    ht_ways_max = Param.Unsigned(96, "Maximum metadata ways per set")
    tracking_window = Param.Unsigned(262144, "Accesses per PID window")
    alpha = Param.Float(0.6, "PID alpha (utility weight)")
    beta = Param.Float(-0.3, "PID beta (delta miss-rate weight)")
    gamma = Param.Float(0.1, "PID gamma (second-derivative weight)")
    theta_plus = Param.Float(0.5, "PID positive threshold")
    theta_minus = Param.Float(-0.25, "PID negative threshold")
    tau = Param.Float(1.2, "Miss-rate explosion threshold")

    # --- LLC-metadata modeling (paper-faithful capacity/bandwidth path) ---
    enable_llc_metadata = Param.Bool(
        False,
        "Model Kairos metadata as real traffic to the mem-side cache (L3)",
    )
    metadata_base_addr = Param.Addr(
        0x8000000000,
        "Base physical address of the shadow region used for metadata "
        "traffic (must be covered by a memory responder)",
    )
    llc_metadata_ways_init = Param.Unsigned(
        4, "Initial number of LLC physical ways reserved for metadata"
    )
    llc_metadata_ways_min = Param.Unsigned(
        1, "Min number of LLC ways reserved for metadata"
    )
    llc_metadata_ways_max = Param.Unsigned(
        8, "Max number of LLC ways reserved for metadata"
    )
    llc_partition = Param.WayPartitioningPolicy(
        NULL,
        "LLC WayPartitioningPolicy this prefetcher drives during PID resizes",
    )
    llc_metadata_initial_ways = VectorParam.Unsigned(
        [],
        "Initial LLC way indices belonging to the metadata partition "
        "(must match the policy's initial allocation for partition_id=1)",
    )


class FetchDirectedPrefetcher(BasePrefetcher):
    type = "FetchDirectedPrefetcher"
    cxx_class = "gem5::prefetch::FetchDirectedPrefetcher"
    cxx_header = "mem/cache/prefetch/fdp.hh"
    cxx_exports = [PyBindMethod("setCache")]

    def __init__(self, **kwargs):
        super().__init__(**kwargs)
        self._cache = None

    def regProbeListeners(self):
        if self._cache:
            self.getCCObject().setCache(self._cache.getCCObject())
        super().regProbeListeners()

    def registerCache(self, simObj):
        if not isinstance(simObj, SimObject):
            raise TypeError("argument must be a SimObject type")
        self._cache = simObj

    cpu = Param.BaseCPU(Parent.any, "The CPU to train the predictor")

    latency = Param.Cycles(1, "Latency for generated prefetches")
    pfq_size = Param.Unsigned(64, "Maximum number of queued prefetches")
    tq_size = Param.Unsigned(64, "Maximum number of outstanding translations")

    mark_req_as_prefetch = Param.Bool(
        True,
        "Mark memory requests as prefetches. Allows different handlings of "
        "request. E.g. the Arm TLB drops prefetch requests on a miss.",
    )
    squash_prefetches = Param.Bool(
        True,
        "Squash the prefetch associated with a fetch target in case it gets "
        "removded fron the the FTQ (Fetch consumes it or a pipeline flush).",
    )
    cache_snoop = Param.Bool(
        True,
        "Snoop the icache (if present) and do not enqueue prefetches for "
        "blocks already in the cache.",
    )


class StreamlinePrefetcher(BasePrefetcher):
    type = "StreamlinePrefetcher"
    cxx_class = "gem5::prefetch::Streamline"
    cxx_header = "mem/cache/prefetch/streamline.hh"

    prefetch_on_access = True

    training_unit_assoc = Param.Int(8, "Associativity of the training unit")
    training_unit_entries = Param.MemorySize(
        "256", "Number of per-PC training-unit entries"
    )
    metadata_store_assoc = Param.Int(
        8, "Number of LLC ways reserved per active metadata set"
    )
    metadata_store_entries = Param.MemorySize(
        "16384", "Maximum number of 64B metadata blocks in the store"
    )
    metadata_base_addr = Param.Addr(
        0x8000000000, "Base address of the Streamline shadow metadata region"
    )
    metadata_line_stride = Param.Unsigned(
        2048,
        "Number of LLC cache lines separating Streamline partial-tag groups",
    )
    metadata_buffer_entries = Param.Int(
        3, "Number of per-PC buffered metadata entries"
    )
    max_degree = Param.Int(4, "Maximum Streamline prefetch degree")
    epoch_size = Param.Int(1024, "Per-PC instability epoch for degree control")
    insertions_low_thresh = Param.Int(
        400, "Insertion threshold for degree four"
    )
    insertions_mid_thresh = Param.Int(
        600, "Insertion threshold for degree three"
    )
    insertions_high_thresh = Param.Int(
        800, "Insertion threshold for degree two"
    )
    metadata_port = RequestPort(
        "Dedicated request port for Streamline LLC metadata traffic"
    )
    llc_partitioning_policy = Param.StreamlinePartitioningPolicy(
        NULL, "Runtime Streamline LLC metadata partition policy"
    )

    @cxxMethod
    def debugMetadataEntryTargetCount(self):
        pass

    @cxxMethod
    def debugTriggerFields(self, trigger_hash):
        pass

    @cxxMethod
    def debugDescribeStreamEntry(self, trigger_hash, targets):
        pass

    @cxxMethod
    def debugAppendTrainingAddress(self, current_stream, address):
        pass

    @cxxMethod
    def debugAlignStreams(self, old_stream, new_stream):
        pass

    @cxxMethod
    def debugDegreeForInsertions(
        self,
        insertions,
        max_degree=4,
        low_insertion_threshold=400,
        mid_insertion_threshold=600,
        high_insertion_threshold=800,
    ):
        pass

    @cxxMethod
    def debugMetadataSetCount(self, metadata_entries, metadata_assoc):
        pass

    @cxxMethod
    def debugActiveMetadataSetCount(
        self, partition_level, max_metadata_sets, sample_set_count=64
    ):
        pass

    @cxxMethod
    def debugIsMetadataSetActive(
        self,
        metadata_set,
        partition_level,
        max_metadata_sets,
        sample_set_count=64,
    ):
        pass

    @cxxMethod
    def debugMetadataLineAddress(
        self,
        metadata_base,
        metadata_line_stride,
        max_metadata_sets,
        metadata_set,
        partial_tag,
    ):
        pass

    @cxxMethod
    def debugRuntimeMetadataLineAddress(self, address):
        pass

    @cxxMethod
    def debugChooseMetadataVictim(self, etrs, valids):
        pass

    @cxxMethod
    def debugMetadataSamplerCoordinates(self, metadata_set):
        pass

    @cxxMethod
    def debugTrainMetadataSampler(self, metadata_set, stream_entry, pc):
        pass

    @cxxMethod
    def debugPredictMetadataSamplerEtr(self, metadata_set, stream_entry):
        pass

    @cxxMethod
    def debugChooseMetadataVictimForEntries(
        self, metadata_set, flattened_entries
    ):
        pass

    @cxxMethod
    def debugMetadataHitScore(self, accuracy):
        pass

    @cxxMethod
    def debugSelectPartitionLevel(self, scores, current_level):
        pass

    @cxxMethod
    def debugSampledPartitionLevel(
        self, metadata_set, max_metadata_sets, sample_set_count=64
    ):
        pass

    @cxxMethod
    def debugPackMetadataBlock(self, entries):
        pass

    @cxxMethod
    def debugUnpackMetadataBlock(self, packed_block):
        pass

    @cxxMethod
    def debugUpdateMetadataBuffer(
        self, current_buffer, stream_entry, buffer_entries=3
    ):
        pass

    @cxxMethod
    def debugPlanBufferedPrefetch(self, current_buffer, address, degree=4):
        pass

    @cxxMethod
    def debugNeedsMetadataRead(self, current_buffer, address):
        pass

    @cxxMethod
    def debugRecordPartitionSample(self, partition_level, score):
        pass

    @cxxMethod
    def debugCurrentPartitionLevel(self):
        pass

    @cxxMethod
    def debugCurrentPartitionLevelStat(self):
        pass

    @cxxMethod
    def debugPartitionTransitionCount(self):
        pass

    @cxxMethod
    def debugPartitionTransitionsStat(self):
        pass

    @cxxMethod
    def debugPartitionScores(self):
        pass

    @cxxMethod
    def debugPartitionSampledAccesses(self):
        pass

    @cxxMethod
    def debugPartitionUpdateInterval(self):
        pass

    @cxxMethod
    def debugResetRuntimeState(self):
        pass

    @cxxMethod
    def debugObserveAccess(self, pc, address):
        pass


add_citation(
    FetchDirectedPrefetcher,
    """@inproceedings{10.1145/3613424.3614258,
  author    = {Schall, David and
               Sandberg, Andreas and
               Grot, Boris},
  title     = {Warming Up a Cold Front-End with Ignite},
  year      = {2023},
  publisher = {Association for Computing Machinery},
  address   = {Toronto, ON, Canada},
  doi       = {10.1145/3613424.3614258},
  booktitle = {Proceedings of the 56th Annual IEEE/ACM International Symposium on Microarchitecture (MICRO '23)},
  series    = {MICRO'23}
}
""",
)
