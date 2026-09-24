#!/usr/bin/env python

# Copyright Contributors to the OpenImageIO project.
# SPDX-License-Identifier: Apache-2.0
# https://github.com/AcademySoftwareFoundation/OpenImageIO

# Run oiiotool with OpenColorIO disabled, so that the OpenEXR writer keeps
# chromaticities it would otherwise drop beside a colorInteropID with other
# primaries: a file another writer could have made.

import os
import subprocess
import sys

subprocess.run(sys.argv[1:], env=dict(os.environ, OIIO_DISABLE_OCIO="1"),
               check=True)
