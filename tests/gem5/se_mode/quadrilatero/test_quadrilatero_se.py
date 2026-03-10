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
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

import re

from gem5.fixture import MakeFixture, MakeTarget
from testlib import *


exit_verifier = verifier.MatchRegex(
    re.compile(r"Exiting @ tick \d+ because exiting with last active thread context\.")
)

cpu_types = ("atomic", "o3")

workloads = (
    ("quadrilatero-fmmacc-b", "quadrilatero-fmmacc-b"),
    ("quadrilatero-fmmacc-h", "quadrilatero-fmmacc-h"),
    ("quadrilatero-fmmacc-s", "quadrilatero-fmmacc-s"),
    ("quadrilatero-mmada-h", "quadrilatero-mmada-h"),
    ("quadrilatero-mmaqa-b", "quadrilatero-mmaqa-b"),
    ("quadrilatero-smoke", "quadrilatero-smoke"),
    ("quadrilatero-matmul", "quadrilatero-matmul"),
    ("quadrilatero-store-pack", "quadrilatero-store-pack"),
    ("quadrilatero-xheep-matmul", "quadrilatero-xheep-matmul"),
)


for prog_dir, binary in workloads:
    make_dir = joinpath(
        config.base_dir, "tests", "test-progs", prog_dir, "src"
    )
    make_fixture = MakeFixture(make_dir)
    workload_binary = MakeTarget(binary, make_fixture=make_fixture)
    binary_path = joinpath(make_dir, binary)

    for cpu in cpu_types:
        gem5_verify_config(
            name=f"test-riscv-{binary}-{cpu}-se",
            fixtures=(workload_binary,),
            verifiers=(exit_verifier,),
            config=joinpath(
                config.base_dir,
                "tests",
                "gem5",
                "se_mode",
                "quadrilatero",
                "configs",
                "local_binary_run.py",
            ),
            config_args=[binary_path, cpu],
            valid_isas=(constants.all_compiled_tag,),
            valid_hosts=constants.supported_hosts,
            length=constants.quick_tag,
        )
