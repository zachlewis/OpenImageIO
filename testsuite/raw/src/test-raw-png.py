#!/usr/bin/env python

# Copyright Contributors to the OpenImageIO project.
# SPDX-License-Identifier: Apache-2.0
# https://github.com/AcademySoftwareFoundation/OpenImageIO

# What a PNG written from a RAW decode claims about its color. A camera-native
# decode names nothing, so it gets no color chunk, where an image that says
# nothing at all still gets the sRGB chunks. Wide describes itself, so its
# chromaticities and linear transfer function become cHRM and gAMA. The chunks
# are parsed from the file, because the PNG reader does not read cHRM. Debug
# output is on so that a warning about lost color would show.

import os
from pathlib import Path
import subprocess
import sys

oiiotool, raw = sys.argv[1:3]
debug_environment = dict(os.environ, OPENIMAGEIO_DEBUG="1")


def oiio(*argv):
    result = subprocess.run([oiiotool, *argv], env=debug_environment,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True)
    assert result.returncode == 0, result.stdout
    return result.stdout


def chunks(filename):
    """The (type, contents) of each chunk of a PNG file, in order."""
    data = Path(filename).read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", filename
    at = 8
    while at + 8 <= len(data):
        length = int.from_bytes(data[at:at + 4], "big")
        yield data[at + 4:at + 8].decode("latin-1"), data[at + 8:at + 8 + length]
        at += 12 + length


def color_chunks(filename):
    return {kind: raw for kind, raw in chunks(filename)
            if kind in ("cHRM", "gAMA", "cICP", "sRGB", "iCCP")}


def write(filename, *argv):
    """Write a PNG and check that no color was reported lost."""
    out = oiio(*argv, "-d", "uint16", "-o", filename)
    assert "PNG cannot" not in out, (filename, out)
    return color_chunks(filename)


def decode(colorspace, filename):
    """Decode the RAW file to a PNG, and check it holds the decoded pixels."""
    decoded = ["--iconfig", "raw:ColorSpace", colorspace, "--iconfig",
               "raw:half_size", "1", raw, "--cut", "64x64+256+256"]
    color = write(filename, *decoded)
    assert "PASS" in oiio(filename, *decoded, "--diff"), filename
    return color


native = decode("raw", "raw-native.png")
assert native == {}, native

wide = decode("Wide", "raw-wide.png")
assert set(wide) == {"cHRM", "gAMA"}, set(wide)
# cHRM is white, red, green, blue, in units of 1/100000.
chrm = [int.from_bytes(wide["cHRM"][i:i + 4], "big") / 100000.0
        for i in range(0, 32, 4)]
for got, want in zip(chrm[2:] + chrm[:2],
                     [0.735433, 0.260745, 0.095528, 0.840843, 0.151495,
                      0.023330, 0.3127, 0.3290]):
    assert abs(got - want) <= 1.0e-5, (chrm, want)
assert int.from_bytes(wide["gAMA"], "big") == 100000, wide["gAMA"]

# Only "unknown" keeps the sRGB default away.
silent = ["--create", "4x4", "3"]
assert set(write("silent.png", *silent)) == {"sRGB", "gAMA", "cHRM"}
assert write("unknown.png", *silent, "--attrib", "colorInteropID",
             "unknown") == {}
