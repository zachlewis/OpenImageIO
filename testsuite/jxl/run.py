#!/usr/bin/env python

# Copyright Contributors to the OpenImageIO project.
# SPDX-License-Identifier: Apache-2.0
# https://github.com/AcademySoftwareFoundation/OpenImageIO


redirect = " >> out.txt 2>&1 "

# Exercise the writer's structured color profiles through libjxl's native
# ORIGINAL/DATA APIs, and compare every lossless output to its source pixels.
command += run_app(
    pythonbin + ' src/test-jxl-color.py "' + oiio_app("oiiotool").strip()
    + '" "' + oiio_app("idiff").strip() + '" "'
    + oiio_app("jxl_profile_test").strip() + '" "' + OIIO_PROJECT_ROOT
    + '/src/libOpenImageIO/interop-identities-config.ocio" "'
    + test_source_dir + '/ref/test-jxl.icc" "'
    + test_source_dir + '/src"'
)

# Test adding and extracting ICC profiles
command += oiiotool ("../common/tahoe-tiny.tif --iccread ref/test-jxl.icc -o tahoe-icc.jxl")
command += info_command ("tahoe-icc.jxl", safematch=True)
command += oiiotool ("tahoe-icc.jxl --iccwrite test-jxl.icc")

command += oiiotool ("../common/tahoe-tiny.tif --cicp \"9,16,9,1\" -o tahoe-cicp-pq.jxl")
command += info_command ("tahoe-cicp-pq.jxl", safematch=True)

# JPEG XL cannot represent CICP transfer 17, so describe DCI-P3 by its color
# space, which libjxl stores with its pure gamma 2.6 DCI transfer.
command += oiiotool ("../common/tahoe-tiny.tif --attrib oiio:ColorSpace oiio:g26_p3dci_display -o tahoe-cicp-dcip3.jxl")
command += info_command ("tahoe-cicp-dcip3.jxl", safematch=True)

command += oiiotool ("../common/tahoe-tiny.tif --cicp \"12,13,0,1\" -o tahoe-cicp-displayp3.jxl")
command += info_command ("tahoe-cicp-displayp3.jxl", safematch=True)

# Corrupt input that previously triggered an oversized allocation path in JXL decode
command += oiiotool ("-info -oiioattrib limits:imagesize_MB 16384 src/crash-bfd2220.jxl", failureok=True)

# Truncated codestream: the decoder must report a clean error, not read
# uninitialized input or leave a null decode buffer for a later scanline read.
command += info_command ("src/truncated.jxl", failureok=True, safematch=True)

outputs = [
            "test-jxl.icc",
            "out.txt"
]
