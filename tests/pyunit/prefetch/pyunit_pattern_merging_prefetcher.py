# Copyright (c) 2026
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
# INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

import unittest
from pathlib import Path

from m5.objects import PatternMergingPrefetcher


class PatternMergingPrefetcherTestSuite(unittest.TestCase):
    def test_paper_default_parameters_are_exposed(self):
        pf = PatternMergingPrefetcher()

        self.assertEqual(int(pf.region_size), 4096)
        self.assertEqual(int(pf.pattern_length), 64)
        self.assertEqual(int(pf.opt_entries), 64)
        self.assertEqual(int(pf.ppt_entries), 32)
        self.assertEqual(int(pf.ft_entries), 64)
        self.assertEqual(int(pf.at_entries), 32)
        self.assertEqual(int(pf.ft_assoc), 8)
        self.assertEqual(int(pf.at_assoc), 2)
        self.assertEqual(int(pf.pb_assoc), 1)
        self.assertEqual(int(pf.counter_bits), 5)
        self.assertEqual(int(pf.ppt_monitoring_range), 2)
        self.assertEqual(int(pf.l1_threshold_percent), 50)
        self.assertEqual(int(pf.l2_threshold_percent), 15)
        self.assertEqual(int(pf.l2_prefetch_skip_cache_levels), 1)
        self.assertEqual(int(pf.llc_prefetch_skip_cache_levels), 2)
        self.assertFalse(pf.on_write.value)

    def test_fill_level_metadata_is_not_exposed_as_public_request_api(self):
        repo_root = Path(__file__).resolve().parents[3]

        request_hh = (repo_root / "src/mem/request.hh").read_text()
        queued_hh = (
            repo_root / "src/mem/cache/prefetch/queued.hh"
        ).read_text()
        queued_cc = (
            repo_root / "src/mem/cache/prefetch/queued.cc"
        ).read_text()

        self.assertNotIn("PREFETCH_SKIP_LEVEL", request_hh)
        self.assertNotIn("getPrefetchSkipCacheLevels", request_hh)
        self.assertNotIn("setPrefetchSkipCacheLevels", request_hh)
        self.assertIn(
            "using AddrPriority = std::pair<Addr, int32_t>;",
            queued_hh,
        )
        self.assertNotIn("struct AddrPriority", queued_hh)
        self.assertNotIn("addr_prio.skipCacheLevels", queued_cc)
