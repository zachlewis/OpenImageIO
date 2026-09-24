#!/usr/bin/env python

# Copyright Contributors to the OpenImageIO project.
# SPDX-License-Identifier: Apache-2.0
# https://github.com/AcademySoftwareFoundation/OpenImageIO

redirect = ' >> out.txt 2>&1 '

imagedir = OIIO_TESTSUITE_IMAGEDIR + "/pnm"

for f in [ "bw-ascii.pbm", "bw-binary.pbm",
           "grey-ascii.pgm", "grey-binary.pgm",
           "rgb-ascii.ppm", "rgb-binary.ppm" ] :
    command += rw_command ("src", f)

# We can't yet write PFM files, so just get the hashes and call it a day
files = [ "test-1.pfm", "test-2.pfm", "test-3.pfm" ]
for f in files:
    command += info_command (imagedir + "/" + f,
                             safematch=True, hash=True)

# Damaged files
files = [ "src/bad-4552.pgm", "src/bad-4553.pgm" ]
for f in files:
    command += info_command (f, extraargs="--oiioattrib try_all_readers 0 --printstats", failureok=True)

# Decompression bomb: a 19-byte header declaring a ~4 GB image (65000x65000).
# The compression-ratio guard must reject it before the caller allocates the
# full pixel buffer.
command += info_command ("src/bomb-65000.pgm", failureok=True, safematch=True)

# Distinguish unchanged float PFM samples from integer BT.709 PNM samples.
import struct
with open("normalization.pfm", "wb") as f:
    f.write(b"PF\n1 1\n-1.0\n" + struct.pack("<fff", 0.25, 0.5, 1.0))
with open("normalization.ppm", "wb") as f:
    f.write(b"P6\n1 1\n255\n" + bytes((0, 255, 0)))
outputs += ["normalization.txt"]
for i, filename in enumerate(("normalization.pfm", "normalization.ppm")):
    command += run_app(
        oiio_app("oiiotool") + " " + filename
        + ' --echo "{TOP.\'oiio:ColorSpace\'} {TOP.AVGCOLOR}"'
        + (" > " if i == 0 else " >> ") + "normalization.txt 2>&1",
        silent=True,
    )
