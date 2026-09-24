#!/usr/bin/env python

# Copyright Contributors to the OpenImageIO project.
# SPDX-License-Identifier: Apache-2.0

try:
    import OpenImageIO as oiio
except ImportError:
    # Nothing to check in a build without the Python bindings.
    raise SystemExit(0)


first = oiio.ImageSpec(1, 1, 3, oiio.HALF)
first.attribute("oiio:ColorSpace", "lin_rec709_scene")
later = oiio.ImageSpec(1, 1, 3, oiio.HALF)
later.attribute("oiio:Gamma", 1.0)
later.attribute("chromaticities", "float[8]",
                (0.713, 0.293, 0.165, 0.830,
                 0.128, 0.044, 0.32168, 0.33767))

out = oiio.ImageOutput.create("output-spec.exr")
assert out and out.open("output-spec.exr", (first, later)), out.geterror()
image = oiio.ImageBufAlgo.zero(first.roi)
assert image.write(out), image.geterror()
assert out.open("output-spec.exr", later, "AppendSubimage"), out.geterror()
assert out.spec().getattribute("chromaticities") is None, out.spec()
assert image.write(out), image.geterror()
assert out.close(), out.geterror()
