#!/usr/bin/env python

# Copyright Contributors to the OpenImageIO project.
# SPDX-License-Identifier: Apache-2.0

command += run_app(
    oiio_app("oiiotool")
    + "--colorconfig src/stats.ocio --pattern constant:color=0.5,0.5,0.5 1x1 3 "
    + "--colorconvert Encoded Reference --runstats > runstats-color.txt 2>&1",
    silent=True,
)
command += run_app(
    oiio_app("oiiotool")
    + "--runstats --echo uncolored > runstats-plain.txt 2>&1",
    silent=True,
)
command += oiiotool('--echo "=== runstats color metrics ==="')
command += run_app(
    'grep -E -o "^  (shared|config)\\.[a-z_.]*" runstats-color.txt'
)
# One color section with color work, none without. grep exits 0 because the
# first file matches.
command += oiiotool('--echo "=== color sections per run ==="')
command += run_app(
    'grep -c "Color config cache" runstats-color.txt runstats-plain.txt'
)

outputs = ["out.txt"]
