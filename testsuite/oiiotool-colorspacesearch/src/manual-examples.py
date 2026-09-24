#!/usr/bin/env python

# Copyright Contributors to the OpenImageIO project.
# SPDX-License-Identifier: Apache-2.0
# https://github.com/AcademySoftwareFoundation/OpenImageIO

# The --colorspacesearch examples the user manual displays. The manual
# includes the block between the markers verbatim, and this script runs every
# line of it against the built-in default config, so the text a reader copies
# is text the testsuite ran, quoting included. Which color spaces a given
# OpenColorIO version's default config holds is not this test's business, so
# only whether a command succeeded and named anything is reported.

import os
import subprocess
import sys

oiiotool = sys.argv[1]

EXAMPLES = r"""
# BEGIN-oiiotool-colorspacesearch-examples
oiiotool --colorspacesearch:gamut=lin_ap1_scene:encoding=scene-linear
oiiotool --colorspacesearch:transfer=srgb_rec709_display
oiiotool --colorspacesearch:gamut=-rec709:state=~display
oiiotool --colorspacesearch:only="ACEScg,Linear Rec.709 (sRGB)"
oiiotool --colorspacesearch:encoding=sdr-video:properties=1
oiiotool --evaloff "--colorspacesearch:transfer='<ExponentTransform> {value: 2.2, direction: inverse}'"
# END-oiiotool-colorspacesearch-examples
"""

environment = dict(os.environ, OCIO="ocio://default")

for line in EXAMPLES.strip().splitlines():
    if line.startswith("#"):
        continue
    # A shell sees exactly what the manual shows, with only the name of the
    # tool replaced by the one this build made, so the quoting is tested too.
    # The replacement is not quoted, exactly as the testsuite's own app
    # substitution does it, so the line does not begin with a quote.
    command = oiiotool + line[len("oiiotool"):]
    result = subprocess.run(command, shell=True, env=environment,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True)
    named = [name for name in result.stdout.splitlines() if name.strip()]
    print(line)
    if result.returncode == 0 and named:
        print("  ok, named one or more color spaces")
    else:
        print("  FAILED: exit {}\n{}".format(result.returncode, result.stdout))
