#!/usr/bin/env python

# Copyright Contributors to the OpenImageIO project.
# SPDX-License-Identifier: BSD-3-Clause and Apache-2.0
# https://github.com/AcademySoftwareFoundation/OpenImageIO

# What the PNG writer records about a color space it cannot write as cICP:
# cHRM and gAMA where they can carry the primaries and the transfer function,
# gAMA alone where no primaries are known (or, with a warning, where a linear
# space's primaries are beyond cHRM), and nothing at all, with a
# warning, where the file cannot carry both. The chunks are parsed from the
# file directly, so the writer is checked apart from the reader. The warning
# needs OPENIMAGEIO_DEBUG, which would change every other command's output, so
# these cases run out of line with their output captured.

import os
from pathlib import Path
import subprocess
import sys

oiiotool = sys.argv[1]
icc_profile = sys.argv[2]
debug_environment = dict(os.environ, OPENIMAGEIO_DEBUG="1")


def oiio(argv, environment=None):
    result = subprocess.run([oiiotool, *argv], env=environment,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True)
    assert result.returncode == 0, result.stdout
    return result.stdout


def write(colorspace, filename, environment=debug_environment):
    return oiio(["--create", "4x4", "3", "-d", "uint16", "--iscolorspace",
                 colorspace, "-o", filename], environment)


def chunks(filename):
    """The (type, contents) of each chunk of a PNG file, in order."""
    data = Path(filename).read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", filename
    at = 8
    while at + 8 <= len(data):
        length = int.from_bytes(data[at:at + 4], "big")
        yield data[at + 4:at + 8].decode("latin-1"), data[at + 8:at + 8 + length]
        at += 12 + length


def chunk(filename, want):
    """The contents of the named PNG chunk, or None if the file has none."""
    return next((raw for kind, raw in chunks(filename) if kind == want), None)


def color_chunks(filename):
    return {kind for kind, raw in chunks(filename)
            if kind in ("cHRM", "gAMA", "cICP", "sRGB", "iCCP")}


def same_pixels(filename, source):
    assert "PASS" in oiio([filename, source, "--diff"]), (filename, source)


def chromaticities(filename):
    """cHRM as (Rx, Ry, Gx, Gy, Bx, By, Wx, Wy), the OIIO order, or None."""
    raw = chunk(filename, "cHRM")
    if raw is None:
        return None
    xy = [int.from_bytes(raw[i:i + 4], "big") / 100000.0
          for i in range(0, 32, 4)]
    return tuple(xy[2:] + xy[:2])


def gamma(filename):
    """The exponent the gAMA chunk encodes, or None."""
    raw = chunk(filename, "gAMA")
    return None if raw is None else round(1.0 / (
        int.from_bytes(raw, "big") / 100000.0), 2)


# Primaries PNG can write as cICP lose nothing. Rec.709 primaries need cHRM
# beside gAMA, while a bare gamma name has no primaries to lose. None warns.
for colorspace, filename in (("lin_p3d65_scene", "quiet-p3d65.png"),
                             ("g22_rec709_display", "quiet-g22.png"),
                             ("Gamma 2.2", "quiet-gamma.png")):
    quiet = write(colorspace, filename)
    assert "WARNING" not in quiet, quiet

# Whether this libPNG can write cICP at all. Without it every color space
# falls back to cHRM, which carries the same primaries.
cicp_supported = "CICP: 12, 8" in oiio(["--info", "-v", "quiet-p3d65.png"])
assert chromaticities("quiet-g22.png") is not None, "quiet-g22.png"
assert chromaticities("quiet-gamma.png") is None, "quiet-gamma.png"
assert ((chromaticities("quiet-p3d65.png") is None) == cicp_supported), \
    "quiet-p3d65.png"

# The same holds where the derived properties are not retained process-wide,
# as on this small config, whose "linear" has exactly Rec.709 primaries.
Path("rec709-linear.ocio").write_text("""ocio_profile_version: 2.3
roles: {default: ref, scene_linear: ref, aces_interchange: ref, cie_xyz_d65_interchange: xyzd65}
file_rules:
  - !<Rule> {name: Default, colorspace: default}
colorspaces:
  - !<ColorSpace>
    name: ref
  - !<ColorSpace>
    name: xyzd65
    to_display_reference: !<MatrixTransform> {matrix: [1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1]}
""")
quiet = write("linear", "quiet-linear.png",
              dict(debug_environment,
                   OCIO=str(Path("rec709-linear.ocio").resolve())))
assert "WARNING" not in quiet, quiet
assert chromaticities("quiet-linear.png") is None, "quiet-linear.png"

# A pure power only a configured space's own properties gives no primaries to
# write beside it (Odd235's are unavailable), so the file carries no color
# chunk rather than implying a gamut the space did not declare.
Path("odd.ocio").write_text("""ocio_profile_version: 2.3
roles: {default: ACES, scene_linear: ACES, aces_interchange: ACES, cie_xyz_d65_interchange: XYZ}
file_rules:
  - !<Rule> {name: Default, colorspace: default}
colorspaces:
  - !<ColorSpace>
    name: ACES
    encoding: scene-linear
  - !<ColorSpace>
    name: Odd235
    encoding: sdr-video
    to_scene_reference: !<GroupTransform>
      children:
        - !<ExponentTransform> {value: 2.35}
        - !<MatrixTransform> {matrix: [0.4395770431186348, 0.3839148953677938, 0.1765080615135714, 0, 0.08960303383941091, 0.8147638539946282, 0.09563311216596088, 0, 0.01741197617524438, 0.1087181258178095, 0.8738698980069461, 0, 0, 0, 0, 1]}
display_colorspaces:
  - !<ColorSpace>
    name: XYZ
    encoding: display-linear
""")
odd_environment = dict(debug_environment, OCIO=str(Path("odd.ocio").resolve()))
warning = write("Odd235", "odd235.png", odd_environment)
assert ('WARNING: PNG cannot represent the primaries of "Odd235"; the file '
        "carries no color chunk at all and uses PNG's sRGB default" in warning), \
    warning
assert color_chunks("odd235.png") == set(), color_chunks("odd235.png")

# A supplied ICC profile describes the primaries, so no cICP is inferred.
oiio(["--create", "4x4", "3", "-d", "uint16", "--iscolorspace",
      "lin_p3d65_scene", "--iccread", icc_profile, "-o", "icc-p3d65.png"])
icc_info = oiio(["--info", "-v", "icc-p3d65.png"])
assert "ICCProfile" in icc_info and "CICP" not in icc_info, icc_info

# A profile supplied for a space cICP cannot carry suppresses cHRM as well:
# the profile states the primaries, and a cHRM disagreeing with it would be
# what a decoder that skips the profile reads.
oiio(["--create", "4x4", "3", "-d", "uint16", "--iscolorspace",
      "lin_ap1_scene", "--iccread", icc_profile, "-o", "icc-ap1.png"])
icc_info = oiio(["--info", "-v", "icc-ap1.png"])
assert "ICCProfile" in icc_info and "CICP" not in icc_info, icc_info
assert chromaticities("icc-ap1.png") is None, "icc-ap1.png"
assert gamma("icc-ap1.png") == 1.0, gamma("icc-ap1.png")

# ACES AP0's blue y is negative, which is outside the xy triangle cHRM can
# hold, so those primaries really are lost and the writer says so.
warning = write("lin_ap0_scene", "warn-ap0.png")
readback = oiio(["--info", "-v", "warn-ap0.png"])
assert "oiio:ColorSpace" not in readback, readback
assert chromaticities("warn-ap0.png") is None, "warn-ap0.png"
expected = ('WARNING: PNG cannot represent the primaries of "lin_ap0_scene"; '
            'the file carries gAMA alone without stating primaries')
assert expected in warning, warning
assert gamma("warn-ap0.png") == 1.0, gamma("warn-ap0.png")

# A camera log encoding has no exponent for gAMA either, so the same file
# carries no color chunk at all, and the warning says so.
warning = write("ocio:slog3_sgamut3_scene", "warn-slog3.png")
expected = ('WARNING: PNG cannot represent the primaries of '
            '"ocio:slog3_sgamut3_scene"; the file carries no color chunk at '
            "all and uses PNG's sRGB default")
assert expected in warning, warning
assert chromaticities("warn-slog3.png") is None, "warn-slog3.png"
assert gamma("warn-slog3.png") is None, gamma("warn-slog3.png")

# ACEScg's are not, so it keeps them.
quiet = write("lin_ap1_scene", "ap1.png")
assert "WARNING" not in quiet, quiet
assert chromaticities("ap1.png") == (0.713, 0.293, 0.165, 0.83, 0.128, 0.044,
                                     0.32168, 0.33767), "ap1.png"
assert gamma("ap1.png") == 1.0, "ap1.png"

# A built-in identity the config (ocio://default) does not define is described
# by the built-in interop-identities config, also after a copy through
# OpenEXR, and keeps its primaries and its transfer function: as cICP where it
# has a code point, otherwise as cHRM plus gAMA.
p3dci = (0.68, 0.32, 0.265, 0.69, 0.15, 0.06, 0.314, 0.351)
p3d65 = (0.68, 0.32, 0.265, 0.69, 0.15, 0.06, 0.3127, 0.329)
rec2020 = (0.708, 0.292, 0.17, 0.797, 0.131, 0.046, 0.3127, 0.329)
for colorspace, xy, exponent in (
        ("lin_p3d65_display", p3d65, 1.0),
        ("oiio:lin_p3dci_display", p3dci, 1.0),
        ("oiio:g24_rec2020_display", rec2020, 2.4),
        ("oiio:g22_p3d65_display", p3d65, 2.2)):
    name = colorspace.replace(":", "-")
    oiio(["--create", "4x4", "3", "-d", "half", "--attrib", "oiio:ColorSpace",
          colorspace, "-o", name + ".exr"])
    output = oiio([name + ".exr", "-d", "uint16", "-o", name + ".png"],
                  debug_environment)
    assert "WARNING" not in output, output
    png = name + ".png"
    if cicp_supported and colorspace == "lin_p3d65_display":
        # The only one of the four with a CICP code point.
        assert "CICP: 12, 8, 0, 1" in oiio(["--info", "-v", png]), png
        assert chromaticities(png) is None, png
    else:
        assert chromaticities(png) == xy, (png, chromaticities(png))
    assert gamma(png) == exponent, (png, gamma(png))

# A linear encoding is written as gamma 1.0 even on a configuration too thin
# to measure, where only the color interop ID says the encoding is linear.
Path("thin.ocio").write_text("""ocio_profile_version: 2.1
roles: {default: ACEScg}
file_rules:
  - !<Rule> {name: Default, colorspace: default}
colorspaces:
  - !<ColorSpace> {name: ACEScg}
  - !<ColorSpace> {name: Lin}
""")
thin = dict(debug_environment, OCIO=str(Path("thin.ocio").resolve()))
write("ACEScg", "thin-acescg.png", thin)
assert gamma("thin-acescg.png") == 1.0, gamma("thin-acescg.png")
assert "oiio:ColorSpace" not in oiio(["--info", "-v", "thin-acescg.png"])
# A color space the configuration says nothing about still gets no gamma.
write("Lin", "thin-lin.png", thin)
assert gamma("thin-lin.png") is None, gamma("thin-lin.png")

# A non-linear identity is not swept up in that: it keeps its own exponent.
write("g22_adobergb_display", "adobergb.png")
# Exactly Adobe RGB's 563/256, which the built-in identity describes, at gAMA's
# precision, not a rounded 2.2.
assert chunk("adobergb.png", "gAMA") == round(100000 * 256 / 563).to_bytes(
    4, "big"), gamma("adobergb.png")
assert chromaticities("adobergb.png") == (0.64, 0.33, 0.21, 0.71, 0.15, 0.06,
                                          0.3127, 0.329), "adobergb.png"

# OIIO_DISABLE_BUILTIN_OCIO_CONFIGS also disables the built-in
# interop-identities config: an identity only it describes publishes no
# colorInteropID, and the PNG writer has no primaries to record or to lose.
for flag, expected in (("0", True), ("1", False)):
    environment = dict(os.environ, OCIO="ocio://default",
                       OIIO_DISABLE_BUILTIN_OCIO_CONFIGS=flag)
    oiio(["--create", "4x4", "3", "-d", "half", "--attrib", "oiio:ColorSpace",
          "oiio:lin_p3dci_display", "-o", "disabled.exr"], environment)
    info = oiio(["--info", "-v", "disabled.exr"])
    assert ('colorInteropID: "oiio:lin_p3dci_display"' in info) == expected, info
    warning = write("lin_ap0_scene", "disabled.png",
                    dict(environment, OPENIMAGEIO_DEBUG="1"))
    assert ("WARNING" in warning) == expected, warning

# A representable gamut on a transfer function neither linear nor a pure power
# gets no color chunk at all: cHRM is written only beside a transfer function
# the file also carries. Pixels are written as they are.
oiio(["--pattern", "fill:top=0.1,0.5,0.9:bottom=0.9,0.5,0.1", "4x4", "3",
      "-d", "uint16", "-o", "source3.tif"])
default_config = dict(debug_environment, OCIO="ocio://default")
names = oiio(["--colorconfiginfo"], default_config)
for colorspace in ("ACEScc", "ACEScct",
                   "sRGB Encoded AP1" if '"sRGB Encoded AP1"' in names
                   else "sRGB Encoded AP1 - Texture"):
    png = colorspace.replace(" ", "-") + ".png"
    warning = oiio(["source3.tif", "--iscolorspace", colorspace, "-o", png],
                   default_config)
    expected = ('OpenImageIO WARNING: PNG cannot represent the transfer '
                'function of "' + colorspace + '", so the file carries no '
                'color chunk')
    assert warning.count("WARNING") == 1 and expected in warning, warning
    assert color_chunks(png) == set(), (png, color_chunks(png))
    same_pixels(png, "source3.tif")

# The same holds for a built-in identity whose primaries the built-in config
# does not state. A pure power keeps gAMA as partial transfer evidence, while
# a non-power curve keeps no color chunk. A Rec.709 gamma keeps cHRM and gAMA.
# A data space states no encoding, so there is nothing to warn about.
for colorspace, lost, chunkless in (
        ("oiio:g22_p3d50_display", "primaries", True),
        ("oiio:g22_adobergbd50_display", "primaries", True),
        ("oiio:g18_prophoto_display", "primaries", True),
        ("dcdm_p3d65_display", "transfer function", True),
        ("oiio:pq_rec709_display", "transfer function", True),
        ("g24_rec709_display", None, False),
        ("data", None, True)):
    png = colorspace.replace(":", "-") + ".png"
    warning = oiio(["source3.tif", "--iscolorspace", colorspace, "-o", png],
                   default_config)
    if lost is None:
        assert "WARNING" not in warning, (png, warning)
    else:
        expected = ('OpenImageIO WARNING: PNG cannot represent the ' + lost
                    + ' of "' + colorspace + '"')
        assert warning.count("WARNING") == 1 and expected in warning, warning
    expected_chunks = set() if chunkless else (
        {"cICP", "gAMA"} if colorspace == "g24_rec709_display" else {"gAMA"})
    assert color_chunks(png) == expected_chunks, (png, color_chunks(png))
    same_pixels(png, "source3.tif")

# P3-D65 on a pure gamma 2.6 is written as cHRM plus gAMA, in color and in
# grey, with pixels written as they are.
p3d65_wire = (68000, 32000, 26500, 69000, 15000, 6000, 31270, 32900)
oiio(["--pattern", "fill:top=0.1:bottom=0.9", "4x4", "1", "-d", "uint16",
      "-o", "source1.tif"])
for channels in (3, 1):
    png = "g26-p3d65-%d.png" % channels
    source = "source%d.tif" % channels
    oiio([source, "--iscolorspace", "g26_p3d65_display", "-o", png])
    assert color_chunks(png) == {"cHRM", "gAMA"}, (png, color_chunks(png))
    assert chromaticities(png) == tuple(v / 100000.0 for v in p3d65_wire), png
    assert chunk(png, "gAMA") == (38462).to_bytes(4, "big"), png
    same_pixels(png, source)

# The reader. gAMA beside a cHRM that states Rec.709 (within the 1e-4 the
# writer compares primaries with) keeps the established Rec.709 gamma label.
# gAMA alone has no stated primaries, and other primaries or primaries with no
# gamma have no stated image state, so they are recorded without a label.
# That never depends on the color config. Files are built from chunks so that
# each case is exactly the one named.
import re
import zlib


def with_chunks(source, filename, extra):
    """Copy PNG `source` to `filename`, its color chunks replaced by `extra`,
    a list of (type, contents), placed before the first IDAT."""
    out = bytearray(b"\x89PNG\r\n\x1a\n")
    for kind, raw in chunks(source):
        if kind in ("cHRM", "gAMA", "cICP", "sRGB", "iCCP"):
            continue
        if kind == "IDAT":
            for extra_kind, extra_raw in extra:
                tag = extra_kind.encode("latin-1")
                out += len(extra_raw).to_bytes(4, "big") + tag + extra_raw
                out += zlib.crc32(tag + extra_raw).to_bytes(4, "big")
            extra = []
        tag = kind.encode("latin-1")
        out += len(raw).to_bytes(4, "big") + tag + raw
        out += zlib.crc32(tag + raw).to_bytes(4, "big")
    Path(filename).write_bytes(bytes(out))


def cHRM(wire):
    """A cHRM chunk from (Rx, Ry, Gx, Gy, Bx, By, Wx, Wy) in wire units."""
    order = wire[6:] + wire[:6]
    return ("cHRM", b"".join(v.to_bytes(4, "big") for v in order))


def gAMA(wire):
    return ("gAMA", wire.to_bytes(4, "big"))


def facts(filename, environment=None):
    """What the reader reports about a file's color."""
    info = oiio(["--info", "-v", filename], environment)
    label = re.search(r'oiio:ColorSpace: "([^"]*)"', info)
    exponent = re.search(r"oiio:Gamma: (\S+)", info)
    xy = re.search(r"\n\s*chromaticities: ([^\n]*)", info)
    return (label.group(1) if label else None,
            float(exponent.group(1)) if exponent else None,
            tuple(float(v) for v in xy.group(1).split(",")) if xy else None,
            "png:sRGB" in info)


rec709_wire = (64000, 33000, 30000, 60000, 15000, 6000, 31270, 32900)
adobe_wire = (64000, 33000, 21000, 71000, 15000, 6000, 31270, 32900)
oiio(["--pattern", "constant:color=0.5,0.5,0.5", "4x4", "3", "-d", "uint8",
      "--iscolorspace", "data", "-o", "grey.png"])
cases = {
    "g26-p3d65-3.png": None,  # written by the writer above
    "r-gamma.png": [gAMA(45455)],
    "r-gamma-rec709.png": [gAMA(45455), cHRM(rec709_wire)],
    # Within the writer's tolerance of Rec.709, and just beyond it
    "r-gamma-near709.png": [gAMA(45455),
                            cHRM((64010,) + rec709_wire[1:])],
    "r-gamma-off709.png": [gAMA(45455), cHRM((64011,) + rec709_wire[1:])],
    "r-gamma-adobe.png": [gAMA(45455), cHRM(adobe_wire)],
    # Adobe RGB's own 563/256, as the writer writes it
    "r-adobe.png": [gAMA(45471), cHRM(adobe_wire)],
    "r-chrm-only.png": [cHRM(adobe_wire)],
    "r-chrm709-only.png": [cHRM(rec709_wire)],
    "r-linear-ap1.png": [gAMA(100000),
                         cHRM((71300, 29300, 16500, 83000, 12800, 4400,
                               32168, 33767))],
    "r-none.png": [],
}
for filename, extra in cases.items():
    if extra is not None:
        with_chunks("grey.png", filename, extra)

# P3-D65 on gamma 2.6, as the writer wrote it: no label, the exact wire
# exponent and the written primaries.
label, exponent, xy, srgb = facts("g26-p3d65-3.png")
assert label is None and not srgb, facts("g26-p3d65-3.png")
assert abs(exponent - 100000 / 38462) < 1e-5, exponent
assert xy == tuple(v / 100000.0 for v in p3d65_wire), xy
# Rec.709 cHRM controls keep the Rec.709 gamma label, with the exact exponent.
for filename in ("r-gamma-rec709.png", "r-gamma-near709.png"):
    label, exponent, xy, srgb = facts(filename)
    assert label == "g22_rec709_scene", (filename, label)
    assert abs(exponent - 100000 / 45455) < 1e-5, (filename, exponent)
# gAMA alone records its exact exponent without inventing primaries.
label, exponent, xy, srgb = facts("r-gamma.png")
assert label is None and xy is None and not srgb, facts("r-gamma.png")
assert abs(exponent - 100000 / 45455) < 1e-5, exponent
# Anything else is recorded and not named, not even as the no-chunk sRGB
# default.
for filename in ("r-gamma-off709.png", "r-gamma-adobe.png", "r-adobe.png",
                 "r-chrm-only.png", "r-chrm709-only.png", "r-linear-ap1.png"):
    label, exponent, xy, srgb = facts(filename)
    assert label is None and not srgb and xy is not None, (filename, label)
assert facts("r-chrm-only.png")[1] is None
assert facts("r-linear-ap1.png")[1] == 1.0
# No chunk at all is still the legacy sRGB default, without png:sRGB.
assert facts("r-none.png") == ("srgb_rec709_scene", None, None, False)

# None of that depends on the color configuration, including one that gives
# a built-in name a conflicting local meaning, or on disabling OCIO or the
# built-in configs.
Path("conflict.ocio").write_text("""ocio_profile_version: 2.3
roles: {default: g22_rec709_scene, scene_linear: lin}
file_rules:
  - !<Rule> {name: Default, colorspace: default}
colorspaces:
  - !<ColorSpace> {name: lin}
  - !<ColorSpace>
    name: g22_rec709_scene
    aliases: [srgb_rec709_scene]
    to_scene_reference: !<ExponentTransform> {value: 2.6}
""")
files = sorted(cases)
reference = [facts(f, dict(os.environ, OCIO="ocio://default")) for f in files]
for setting in ({"OCIO": str(Path("conflict.ocio").resolve())},
                {"OIIO_DISABLE_OCIO": "1"},
                {"OIIO_DISABLE_BUILTIN_OCIO_CONFIGS": "1"}):
    assert [facts(f, dict(os.environ, **setting)) for f in files] == \
        reference, setting

# A consumer resolves the unnamed P3-D65 file from those facts, to the
# configuration's own P3-D65 gamma 2.6 space, and converts from it.
default_config = dict(os.environ, OCIO="ocio://default")
trace = oiio(["--debug", "-i:autocc=1", "g26-p3d65-3.png", "-o", "x.exr"],
             default_config)
assert ('resolve to "P3-D65 - Display" (numeric metadata; resolved)'
        in trace), trace
# PNG prefers a display-referred match, but a linear AP1 file, which only a
# scene-referred space matches, still resolves to it.
trace = oiio(["--debug", "-i:autocc=1", "r-linear-ap1.png", "-o", "x.exr"],
             default_config)
assert 'resolve to "ACEScg" (numeric metadata; resolved)' in trace, trace


def average(argv, environment=default_config):
    stats = oiio([*argv, "--printstats"], environment)
    return [float(v) for v in
            re.search(r"Stats Avg: ([^(]*)", stats).group(1).split()]


converted = average(["g26-p3d65-3.png", "--tocolorspace", "ACEScg"])
explicit = average(["g26-p3d65-3.png", "--colorconvert", "P3-D65 - Display",
                    "ACEScg"])
assert converted == explicit, (converted, explicit)

# gAMA alone has no reader label, but every implicit-source consumer resolves
# its transfer through the shared numeric rule instead of assuming linear.
trace = oiio(["--debug", "-i:autocc=1", "r-gamma.png", "-o", "x.exr"],
             default_config)
gamma_space = re.search(
    r'r-gamma.png resolve to "([^"]+)" \(numeric metadata', trace).group(1)
gamma_expected = average(["r-gamma.png", "--colorconvert", gamma_space,
                          "srgb_rec709_scene"])
assert average(["r-gamma.png", "--tocolorspace", "srgb_rec709_scene"]) == \
    gamma_expected
gamma_look = average(["r-gamma.png", "--ociolook", "", "--tocolorspace",
                      "srgb_rec709_scene"])
gamma_look_explicit = average(
    ["r-gamma.png", "--ociolook:from=" + gamma_space, "", "--tocolorspace",
     "srgb_rec709_scene"])
assert gamma_look == gamma_look_explicit
gamma_display = average(["r-gamma.png", "--ociodisplay", "sRGB - Display",
                         "Un-tone-mapped"])
gamma_display_explicit = average(
    ["r-gamma.png", "--ociodisplay:from=" + gamma_space, "sRGB - Display",
     "Un-tone-mapped"])
assert gamma_display == gamma_display_explicit
for argv in (["--autocc", "r-gamma.png"],
             ["--autocc", "-i:autocc=0", "r-gamma.png"]):
    oiio([*argv, "-o", "gamma.jpg"], default_config)
    assert all(abs(v - 128) <= 2 for v in average(["gamma.jpg"])), \
        (argv, average(["gamma.jpg"]))

# Every consumer that takes an image's own color space reads the Adobe RGB
# file, which has no label, from its gAMA and cHRM, never as scene_linear:
# mid-grey at Adobe RGB's gamma is about 0.504 in sRGB, and about 128 in a
# JPEG. (The file's 8-bit 128 is 0.502, which converts to 0.506.) Each is
# compared with an explicit conversion from the space the resolver names,
# which depends on the configuration.
trace = oiio(["--debug", "-i:autocc=1", "r-adobe.png", "-o", "x.exr"],
             default_config)
adobe = re.search(r'r-adobe.png resolve to "([^"]+)" \(numeric metadata',
                  trace).group(1)
expected = average(["r-adobe.png", "--colorconvert", adobe,
                    "srgb_rec709_scene"])
assert all(abs(v - 0.504) < 0.005 for v in expected), expected
look = ["--ociolook", "", "--tocolorspace", "srgb_rec709_scene"]
for argv in (["--tocolorspace", "srgb_rec709_scene"], look):
    if argv == look and adobe not in names:
        # A look is built by OCIO from configured spaces only, so a space
        # only OpenImageIO's reference knows is refused, not replaced.
        assert "not defined" in subprocess.run(
            [oiiotool, "r-adobe.png", *argv], env=default_config,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True).stdout
        continue
    grey = average(["r-adobe.png", *argv])
    assert grey == expected, (argv, grey, expected)
# The output conversion too, for an input read without one.
for argv in (["--autocc", "r-adobe.png"],
             ["--autocc", "-i:autocc=0", "r-adobe.png"]):
    oiio([*argv, "-o", "adobe.jpg"], default_config)
    assert all(abs(v - 129) <= 1 for v in average(["adobe.jpg"])), \
        (argv, average(["adobe.jpg"]))
display = average(["r-adobe.png", "--ociodisplay", "sRGB - Display",
                   "Un-tone-mapped"])
explicit = average(["r-adobe.png", "--ociodisplay:from=" + adobe,
                    "sRGB - Display", "Un-tone-mapped"])
assert display == explicit, (display, explicit)
# A PNG with primaries and no gamma, or with a gamma no configured space on
# those primaries has (2.2 is not Adobe RGB's 563/256 at gAMA's precision),
# states an encoding nothing here can use, so a conversion refuses it rather
# than choosing one.
for filename in ("r-chrm-only.png", "r-gamma-adobe.png"):
    refused = subprocess.run([oiiotool, filename, "--tocolorspace",
                              "srgb_rec709_scene", "-o", "x.exr"],
                             env=default_config, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, text=True)
    assert refused.returncode != 0 and "Unknown color space name" in \
        refused.stdout, (filename, refused.stdout)

# Copies keep what the file said. Another PNG carries the same cHRM and
# gAMA, not the sRGB chunk an unlabeled image otherwise defaults to, and a
# cHRM with no gamma is not written alone. OpenEXR chromaticities would state
# a linear encoding, so they are not written beside gamma 2.2, and the file
# says "unknown" rather than nothing. Targa's gamma alone would state Rec.709
# primaries, so it is not written either.
oiio(["r-adobe.png", "-o", "copy-adobe.png"])
assert chromaticities("copy-adobe.png") == tuple(
    v / 100000.0 for v in adobe_wire), chromaticities("copy-adobe.png")
assert chunk("copy-adobe.png", "gAMA") == (45471).to_bytes(4, "big")
assert "sRGB" not in color_chunks("copy-adobe.png")
oiio(["r-chrm-only.png", "-o", "copy-chrm-only.png"])
assert color_chunks("copy-chrm-only.png") == set()
oiio(["r-chrm-only.png", "-d", "half", "-o", "copy-chrm-only.exr"])
info = oiio(["--info", "-v", "copy-chrm-only.exr"])
assert "chromaticities" not in info, info
assert 'colorInteropID: "unknown"' in info, info
oiio(["copy-chrm-only.exr", "-d", "uint8", "-o",
      "copy-chrm-only-reread.png"])
info = oiio(["--info", "-v", "copy-chrm-only-reread.png"])
assert "oiio:ColorSpace" not in info, info
assert 'colorInteropID: "unknown"' in info, info
oiio(["r-adobe.png", "-d", "half", "-o", "copy-adobe.exr"])
info = oiio(["--info", "-v", "copy-adobe.exr"])
assert "chromaticities" not in info, info
assert 'colorInteropID: "unknown"' in info, info
oiio(["r-adobe.png", "-o", "copy-adobe.tga"])
assert "g22_rec709_scene" not in oiio(["--info", "-v", "copy-adobe.tga"])
oiio(["r-linear-ap1.png", "-d", "half", "-o", "copy-ap1.exr"])
assert "0.713, 0.293" in oiio(["--info", "-v", "copy-ap1.exr"])

# A colorInteropID of "unknown", in any letter case, is all an image that
# could not be identified says. Copied to PNG it gets no color chunk, rather
# than the sRGB an unlabeled image otherwise defaults to, and nothing is lost
# to warn about. Read back, the file says the same and has no label, so a
# conversion refuses it rather than reading it as sRGB.
for stated in ("unknown", "UNKNOWN"):
    oiio(["--pattern", "constant:color=0.25,0.25,0.25", "4x4", "3", "-d",
          "half", "--attrib", "colorInteropID", stated, "-o", "unknown.exr"])
    warning = oiio(["unknown.exr", "-d", "uint8", "-o", "unknown.png"],
                   debug_environment)
    assert "WARNING" not in warning, warning
    assert color_chunks("unknown.png") == set(), color_chunks("unknown.png")
    assert facts("unknown.png") == (None, None, None, False), \
        facts("unknown.png")
    assert 'colorInteropID: "%s"' % stated in oiio(["--info", "-v",
                                                    "unknown.png"])
    refused = subprocess.run([oiiotool, "unknown.png", "--tocolorspace",
                              "srgb_rec709_scene", "-o", "x.exr"],
                             env=default_config, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, text=True)
    assert refused.returncode != 0 and "Unknown color space name" in \
        refused.stdout, (stated, refused.stdout)

# A caller's spelling has the same case-insensitive unknown semantics as a
# file's value, rather than leaking through to an OCIO-specific error.
for stated in ("unknown", "Unknown"):
    refused = subprocess.run(
        [oiiotool, "--pattern", "constant:color=.5,.5,.5", "1x1", "3",
         "--colorconvert", stated, "srgb_rec709_scene"],
        env=default_config, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True)
    assert refused.returncode != 0 and \
        'Unknown color space name (from="%s"' % stated in refused.stdout, \
        (stated, refused.stdout)

# A conversion leaves no primaries that disagree with its destination, so an
# ordinary Rec.709 PNG converted to ACES2065-1 makes a valid ST 2065-4 file.
oiio(["r-gamma-rec709.png", "--tocolorspace", "lin_ap0_scene", "-d", "half",
      "--compression", "none", "--attrib", "openexr:ACESContainerPolicy",
      "strict", "-o", "aces.exr"], default_config)

# FLIP compares in linear Rec.709, and the unnamed file converts from what it
# states, so it matches its own explicit conversion.
oiio(["r-adobe.png", "--colorconvert", adobe, "lin_rec709_scene",
      "-d", "float", "-o", "adobe-linear.exr"], default_config)
flip = oiio(["r-adobe.png", "adobe-linear.exr", "--flipdiff"],
            default_config)
assert re.search(r"Mean FLIP error\s*=\s*0\n", flip), flip
oiio(["r-gamma.png", "--colorconvert", gamma_space, "lin_rec709_scene",
      "-d", "float", "-o", "gamma-linear.exr"], default_config)
flip = oiio(["r-gamma.png", "gamma-linear.exr", "--flipdiff"], default_config)
assert re.search(r"Mean FLIP error\s*=\s*0\n", flip), flip

# With png:linear_premult, alpha is applied in linear light using the file's
# gamma, which for other primaries is the recorded one, not 1.
oiio(["--pattern", "constant:color=0.5,0.5,0.5,0.5", "4x4", "4", "-d", "uint8",
      "--iscolorspace", "data", "-o", "grey-alpha.png"])
with_chunks("grey-alpha.png", "alpha-709.png", [gAMA(45471), cHRM(rec709_wire)])
with_chunks("grey-alpha.png", "alpha-adobe.png", [gAMA(45471), cHRM(adobe_wire)])
premult = [average(["--oiioattrib", "png:linear_premult", "1", f])
           for f in ("alpha-709.png", "alpha-adobe.png")]
assert all(abs(a - b) < 0.002 for a, b in zip(*premult)), premult
