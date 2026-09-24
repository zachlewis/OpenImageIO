#!/usr/bin/env python

# Copyright Contributors to the OpenImageIO project.
# SPDX-License-Identifier: Apache-2.0
# https://github.com/AcademySoftwareFoundation/OpenImageIO

# Usage: rename-exr-attrib.py file.exr oldname newname
# Renames a header attribute in place. The names must be the same length so
# that no offset in the file moves.

import sys

filename, old, new = sys.argv[1], sys.argv[2].encode(), sys.argv[3].encode()
assert len(old) == len(new)
with open(filename, "rb") as f:
    data = f.read()
assert data.count(old + b"\0") == 1
with open(filename, "wb") as f:
    f.write(data.replace(old + b"\0", new + b"\0"))
